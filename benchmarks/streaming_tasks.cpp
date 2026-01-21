#include <benchmark/benchmark.h>
#include <glm/fwd.hpp>
#include <iostream>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <atomic>
#include <condition_variable>

#include <functional>
#include <filesystem>
#include <fstream>
#include <future>
#include <latch>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <new>
#include <queue>
#include <xmmintrin.h>

#include "binalloc/utils.h"

#define MIN_ITERATION_RANGE (1U << 14U)
#define MAX_ITERATION_RANGE (1U << 22U)

#define THREAD_COUNT 7

#if defined (__linux)
#include <pthread.h>

int is_p_core(int cpu_id) {
    char  path[256];
    char  buffer[256];
    FILE *fp;

    // Check CPU topology through sysfs
    snprintf(
        path,
        sizeof(path),
        "/sys/devices/system/cpu/cpu%d/topology/core_type",
        cpu_id);

    fp = fopen(path, "r");
    if(!fp) return -1; // Error reading

    if(fgets(buffer, sizeof(buffer), fp) != NULL) {
        fclose(fp);
        // On hybrid architectures, typically:
        // 0 = P-core
        // 1 = E-core
        return (atoi(buffer) == 0);
    }

    fclose(fp);
    return -1;
}

std::vector<std::size_t> get_physical_cores() {
    std::vector<std::size_t> physical_cores;

    auto core_count = std::thread::hardware_concurrency();

    for(std::size_t i = 0; i < core_count; ++i) {
        if(is_p_core) {
            physical_cores.push_back(i);
        }
    }

    return physical_cores;
}

void set_thread_affinity(std::size_t core_id) {
    // set cpu affinity
    auto native_thread = pthread_self();

    cpu_set_t cpu_set;

    CPU_ZERO(&cpu_set);
    CPU_SET(core_id, &cpu_set);

    auto ret = pthread_setaffinity_np(native_thread, sizeof(cpu_set_t), &cpu_set);

    if(ret != 0) {
        std::cerr << "cpu core affinity failed" << std::endl;
    }
}

#elif defined (_WIN32)
#include <Windows.h>

std::vector<std::size_t> get_physical_cores() {
    std::vector<std::size_t> cores;

    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer          = nullptr;
    DWORD                                 length_in_bytes = 0;

    // get size of buffer;
    if(FAILED(
        GetLogicalProcessorInformation(
            nullptr,
            &length_in_bytes))) {
        std::cerr << "get cpu physical cores failed" << std::endl;
        return {};
    }

    buffer = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION>(malloc(length_in_bytes));

    if(!buffer) {
        std::cerr << "get cpu physical cores allocation failed" << std::endl;
        return {};
    }

    if(FAILED(
        GetLogicalProcessorInformation(
            buffer,
            &length_in_bytes))) {
        std::cerr << "get cpu physical cores failed" << std::endl;
        return {};
    }

    auto *it  = buffer;
    auto *end = it + length_in_bytes / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);

    while(it < end) {
        if(it->Relationship == RelationProcessorCore) {
            unsigned long id;

            if(BitScanForward64(&id, it->ProcessorMask)) {
                cores.push_back(id);
            }
        }

        ++it;
    }

    free(buffer);

    return cores;
}

void set_thread_affinity(std::size_t core_id) {
    // works only up to 64 processors
    auto handle = GetCurrentThread();
    auto mask   = (1 << core_id);

    if(!SetThreadAffinityMask(handle, mask)) {
        std::cerr << "set cpu core affinity failed" << std::endl;
    }
}

#endif

using namespace glm;

struct transformer {
    glm::mat4 matrix;

    glm::vec3 pos;
    glm::vec3 rotation;
    float     scale;

    inline void update_matrix() {
        benchmark::DoNotOptimize(matrix);
        matrix = glm::mat4{1.0f};

        matrix = glm::scale(matrix, glm::vec3(scale, scale, scale));
        matrix = glm::rotate(matrix, 1.0f, rotation);
        matrix = glm::translate(matrix, pos);

        matrix = glm::inverse(matrix);
        matrix = glm::transpose(matrix);
    }
};

std::vector<transformer> test_data(MAX_ITERATION_RANGE);

namespace proto_e {
    struct spin_mutex {
        void lock() noexcept {
            for(;;) {
                // Optimistically assume the lock is free on the first try
                if(!_lock.exchange(true, std::memory_order_acquire)) {
                    return;
                }
                // Wait for lock to be released without generating cache misses
                while(_lock.load(std::memory_order_relaxed)) {
                    // Issue X86 PAUSE or ARM YIELD instruction to reduce contention between
                    // hyper-threads

                    _mm_pause();
                }
            }
        }

        bool try_lock() noexcept {
            // First do a relaxed load to check if lock is free in order to prevent
            // unnecessary cache misses if someone does while(!try_lock())
            return !_lock.load(std::memory_order_relaxed) &&
                    !_lock.exchange(true, std::memory_order_acquire);
        }

        void unlock() noexcept {
            _lock.store(false, std::memory_order_release);
        }

    private:
        std::atomic_bool _lock{false};
    };

    using task_callback = void (*)(void *data_begin, void *data_end);

    struct alignas(64) task {
    public:
        inline void call() const {
            callback(data_begin, data_end);
            --(parent->unresolved_children);
        }

        task_callback callback;

        void *data_begin;
        void *data_end;

        task *parent;

        std::atomic_size_t
        unresolved_children;
    };

    struct alignas(64) lane {
    public:
        void push(task *t) noexcept {
            auto lock = std::scoped_lock(queue_mutex);

            tasks[back & mask] = t;
            ++back;
        }

        task *pop() noexcept {
            auto lock = std::scoped_lock(queue_mutex);

            if(back == front) {
                return nullptr;
            }

            auto *t = tasks[back - 1 & mask];
            --back;

            return t;
        }

        task *steal() noexcept {
            auto lock = std::scoped_lock(queue_mutex);

            if(back == front) {
                return nullptr;
            }

            auto *t = tasks[front & mask];
            ++front;

            return t;
        }

        inline task *alloc() noexcept {
            ++task_buffer_index;
            return task_buffer.get() + task_buffer_index;
        }

        inline void alloc_free() noexcept {
            task_buffer_index = 0;
        }

        uint64_t   front, back;
        spin_mutex queue_mutex;

        // off critical path ( good ? / bad ? )
        std::unique_ptr<task *[]> tasks{new task *[8192]};
        std::unique_ptr<task[]>   task_buffer{new task[8192]};

        std::size_t task_buffer_index{0};

        bool running{false};

        static constexpr std::size_t mask{8192 - 1};
    };

    struct executor {
    public:
        explicit executor() :
            physical_cores(get_physical_cores()),
            lanes(physical_cores.size()),
            workers(physical_cores.size()) {}

        ~executor() {
            stop();
        }

        inline void stop() {
            for(auto & lane: lanes) {
                lane.running = false;
            }

            for(auto & worker: workers) {
                if(worker.joinable())
                    worker.join();
            }
        }

        inline static void run_lane(std::vector<lane> & lanes, lane & l, unsigned i) {
            auto *t = l.pop();

            // if no job available, try steal
            if(!t) {
                auto rand_index = std::chrono::high_resolution_clock::now().time_since_epoch().count() % (lanes.size());

                if(rand_index != i)
                    t = lanes[rand_index].steal();
            }

            if(t) {
                t->call();
            } else {
                std::this_thread::yield();
            }
        }

        void run() {
            if(lanes[0].running)
                return;

            lanes[0].running = true;

            // init workers
            for(unsigned i = 1; i < physical_cores.size(); ++i) {
                workers[i] = std::thread(
                    [this, i, core=physical_cores[i]]() {
                        // set cpu affinity
                        set_thread_affinity(core);

                        auto & lane = lanes[i];

                        // run
                        lane.running = true;

                        while(lane.running)
                            run_lane(lanes, lane, i);
                    });
            }
        }

        void wait_for(task *t) {
            auto & lane = get_this_lane();

            while(t->unresolved_children.load(std::memory_order_relaxed) != 0) {
                run_lane(lanes, lane, 0);
            }
        }

        template <typename _data_t>
        inline void run_parallel(task_callback callback, _data_t *data, std::size_t length) {
            auto & lane = get_this_lane();

            //unsigned job_div = std::sqrt(length);
            unsigned job_div = this->physical_cores.size();

            task *parent                = lane.alloc();
            parent->unresolved_children = job_div;

            auto stride = length / job_div;
            auto rem    = length % job_div;

            for(unsigned i = 0; i < job_div; ++i) {
                auto offset = i * stride;

                if(i == job_div - 1)
                    stride = stride + rem;

                task *t = lane.alloc();

                t->callback   = callback;
                t->data_begin = data + offset;
                t->data_end   = data + offset + stride;

                t->parent = parent;

                lane.push(t);
            }

            wait_for(parent);

            for(auto & f_lane: lanes)
                f_lane.alloc_free();
        }

        template <typename _data_t>
        inline void run_parallel_many(
            _data_t *   data,
            std::size_t length,
            void (*     callback)(_data_t *begin, _data_t *end)) {
            auto & lane = get_this_lane();

            unsigned job_div = std::sqrt(length / physical_cores.size());

            task *parent                = lane.alloc();
            parent->unresolved_children = job_div;

            auto stride = length / job_div;
            auto rem    = length % job_div;

            for(unsigned i = 0; i < job_div; ++i) {
                auto offset = i * stride;

                if(i == job_div - 1)
                    stride = stride + rem;

                task *t = lane.alloc();

                t->callback   = (task_callback)callback;
                t->data_begin = data + offset;
                t->data_end   = data + offset + stride;

                t->parent = parent;

                lane.push(t);
            }

            wait_for(parent);

            for(auto & f_lane: lanes)
                f_lane.alloc_free();
        }

        inline lane &get_this_lane() noexcept {
            auto id = std::this_thread::get_id();

            for(int i = 1; i < workers.size(); ++i) {
                if(workers[i].get_id() == id)
                    return lanes[i];
            }

            return lanes[0];
        }

        std::vector<std::size_t> physical_cores;
        std::vector<lane>        lanes;
        std::vector<std::thread> workers;
    };
}

namespace proto_f {
    struct spin_mutex {
        void lock() noexcept {
            for(;;) {
                // Optimistically assume the lock is free on the first try
                if(!_lock.exchange(true, std::memory_order_acquire)) {
                    return;
                }
                // Wait for lock to be released without generating cache misses
                while(_lock.load(std::memory_order_relaxed)) {
                    // Issue X86 PAUSE or ARM YIELD instruction to reduce contention between
                    // hyper-threads

                    _mm_pause();
                }
            }
        }

        bool try_lock() noexcept {
            // First do a relaxed load to check if lock is free in order to prevent
            // unnecessary cache misses if someone does while(!try_lock())
            return !_lock.load(std::memory_order_relaxed) &&
                    !_lock.exchange(true, std::memory_order_acquire);
        }

        void unlock() noexcept {
            _lock.store(false, std::memory_order_release);
        }

    private:
        std::atomic_bool _lock{false};
    };

    using task_callback_t = std::function<void ()>;

    struct exec_lane {
    public:
        ~exec_lane() {
            if(!_task_queue.empty()) {
                std::cerr << "exec_lane destroyed with pending tasks !" << std::endl;
            }
        }

        void push(task_callback_t && task) {
            auto lock = std::scoped_lock(_queue_mutex);
            _task_queue.push_back(std::forward<task_callback_t>(task));
        }

        task_callback_t pop() {
            auto lock = std::scoped_lock(_queue_mutex);

            if(_task_queue.empty()) {
                return {};
            }

            auto task = std::move(_task_queue.back());
            _task_queue.pop_back();

            return task;
        }

        task_callback_t steal() {
            auto lock = std::scoped_lock(_queue_mutex);

            if(_task_queue.empty()) {
                return {};
            }

            auto task = std::move(_task_queue.front());
            _task_queue.pop_front();

            return task;
        }

    private:
        std::deque<task_callback_t> _task_queue;
        spin_mutex                  _queue_mutex;
    };

    //thread_local exec_lane * this_lane = nullptr;

    struct token {
        void resolve() const {
            unresolved->fetch_sub(1, std::memory_order_acq_rel);
        }

        [[nodiscard]] bool done() const { return unresolved->load() == 0; }

        static token make(std::uint64_t const initial_unresolved) {
            token tk;
            *tk.unresolved = initial_unresolved;

            return tk;
        }

        std::shared_ptr<std::atomic_uint64_t> unresolved = std::make_shared<std::atomic_uint64_t>(0);
    };

    struct executor {
    public:
        executor(uint8_t thread_count = THREAD_COUNT) :
            //_init_latch(thread_count),
            _workers(thread_count)
        //_lanes(thread_count)
        {
            for(int i = 0; i < thread_count; ++i) {
                _workers[i] = std::jthread(
                    [this, index = i](std::stop_token token) {
                        // register lane for stealing
                        //exec_lane lane;
                        //this_lane = &lane;
                        //this->_lanes[index] = &lane;

                        // wait for all threads to be initialized
                        //_init_latch.count_down();
                        //_init_latch.wait();

                        // just run until stop requested
                        while(!token.stop_requested()) {
                            //auto task = lane.pop();
                            auto task = this->_global_lane.pop();

                            if(!task) {
                                //                                task = this->steal_task(index);
                                //
                                //                                if (!task) {
                                std::this_thread::yield();
                                continue;
                                //                                }
                            }

                            task();
                        }
                    });
            }
        }

        ~executor() {
            // lanes will be destroyed with thread local storage cleanup
            for(auto & worker: _workers) {
                worker.request_stop();
            }
        }

        token run(std::invocable<void> auto && callback) {
            //auto * lane = this_lane ? this_lane : & _global_lane;

            auto tk = token::make(1);

            _global_lane.push(
                [tk, callback = std::forward<decltype(callback)>(callback)] {
                    callback();
                    tk.resolve();
                });

            return tk;
        }

        template <typename data_type>
        token run_parallel(std::span<data_type> data, std::function<void (std::span<data_type>)> const & task) {
            auto job_div = _workers.size() *
                    4; //static_cast < std::size_t > (std::sqrt (data.size() / _workers.size()));
            //auto * lane = this_lane ? this_lane : & _global_lane;
            auto tk = token::make(job_div);

            auto stride = data.size() / job_div;
            auto rem    = data.size() % job_div;

            for(int i = 0; i < job_div; ++i) {
                auto offset = i * stride;

                if(i == job_div - 1) {
                    stride = stride + rem;
                }

                auto data_span = std::span<data_type>(data.data() + offset, stride);

                _global_lane.push(
                    [tk, data_span, task = task]() {
                        task(data_span);
                        tk.resolve();
                    });
            }

            return tk;
        }

        void wait_for(token const & tk) {
            if(tk.done()) {
                return;
            }

            do {
                // auto task = steal_task (std::numeric_limits<unsigned>::max());
                auto task = _global_lane.pop();

                if(task) {
                    task();
                } else {
                    std::this_thread::yield();
                }
            } while(!tk.done());
        }

    private:
        //        [[nodiscard]] task_callback_t steal_task(unsigned thief_index) {
        //            auto task = _global_lane.steal();
        //
        //            if (task) {
        //                return task;
        //            }
        //
        //            auto lane_count = _lanes.size();
        //            auto rand_index = std::chrono::system_clock::now().time_since_epoch().count() % (lane_count);
        //
        //            // make sure we don't steal from ourselves
        //            if(rand_index == thief_index) {
        //                rand_index = (rand_index + 1) % lane_count;
        //            }
        //
        //            if(auto *victim_lane = _lanes[rand_index]) {
        //                return victim_lane->steal();
        //            }
        //
        //            return {};
        //        }

        //std::latch                  _init_latch;
        std::vector<std::jthread> _workers;
        //        exec_lane *                 _lanes[THREAD_COUNT]{nullptr};
        exec_lane _global_lane;
    };
}

namespace proto_g {
    struct spin_mutex {
        void lock() noexcept {
            for(;;) {
                // Optimistically assume the lock is free on the first try
                if(!_lock.exchange(true, std::memory_order_acquire)) {
                    return;
                }
                // Wait for lock to be released without generating cache misses
                while(_lock.load(std::memory_order_relaxed)) {
                    // Issue X86 PAUSE or ARM YIELD instruction to reduce contention between
                    // hyper-threads

                    _mm_pause();
                }
            }
        }

        bool try_lock() noexcept {
            // First do a relaxed load to check if lock is free in order to prevent
            // unnecessary cache misses if someone does while(!try_lock())
            return !_lock.load(std::memory_order_relaxed) &&
                    !_lock.exchange(true, std::memory_order_acquire);
        }

        void unlock() noexcept {
            _lock.store(false, std::memory_order_release);
        }

    private:
        std::atomic_bool _lock{false};
    };

    struct sync_token {
    public:
        [[nodiscard]] bool done() const { return _unresolved->load() == 0; }

        static sync_token make(std::uint64_t const initial_unresolved) {
            sync_token tk;
            *tk._unresolved = initial_unresolved;

            return tk;
        }

    private:
        friend struct task;

        void resolve() const {
            _unresolved->fetch_sub(1, std::memory_order_acq_rel);
        }

        std::shared_ptr<std::atomic_uint64_t> _unresolved = std::make_shared<std::atomic_uint64_t>(0);
    };

    struct task {
        struct resolve_guard {
            ~resolve_guard() {
                unresolved.fetch_sub(1, std::memory_order_acq_rel);
            }

            std::atomic_uint64_t & unresolved;
        };

        void invoke() const {
            try {
                if (callback) {
                    auto guard = resolve_guard{.unresolved = *tk._unresolved};
                    callback();
                }
            } catch(...) {}
        }

        explicit operator bool () const {
            return static_cast<bool>(callback);
        }

        std::function<void ()> callback;
        sync_token             tk;
    };

    struct executor {
    public:

        explicit executor (uint16_t const thread_count = THREAD_COUNT) :
            _workers(thread_count)
        {
            for(int i = 0; i < thread_count; ++i) {
                // TODO: SETUP CPU AFFINITY
                _workers[i] = std::thread(
                    [this] {
                        for (;;) {
                            task current_task;

                            {
                                auto lock = std::unique_lock(_mutex);

                                this->_cv.wait (
                                    lock,
                                    [this]{ return !this->_task_queue.empty() || !this->_is_running; });

                                if (_task_queue.empty() && !_is_running) {
                                    return;
                                }

                                current_task = std::move(_task_queue.front());
                                _task_queue.pop_front();
                            }

                            current_task.invoke();
                        }
                    });
            }
        }

        ~executor () {
            _is_running.store(false, std::memory_order_release);
            _cv.notify_all();

            for(auto & worker: _workers) {
                if(worker.joinable()) {
                    worker.join();
                }
            }

            if (_task_queue.empty() == false) {
                std::cerr << "executor destroyed with pending tasks !" << std::endl;
            }
        }

        sync_token push (std::function<void ()> && callback) {
            auto tk = sync_token::make(1);

            {
                auto lock = std::scoped_lock(_mutex);

                _task_queue.push_back(
                    task{
                        .callback = std::forward<std::function<void ()>>(callback),
                        .tk       = tk});
            }

            _cv.notify_one();

            return tk;
        }

        template < typename data_type >
        sync_token push_parallel (std::span < data_type > data, std::function < void (std::span < data_type >) > const & task_fn) {
            // divide work in chunks of multiples of pages
            auto job_div = _workers.size() * 16; // find a decent division factor

            // token for synchronization
            auto tk = sync_token::make(job_div);

            auto stride = data.size() / job_div;
            auto rem    = data.size() % job_div;

            // ----------
            auto lock = std::scoped_lock(_mutex);

            // push tasks
            for(int i = 0; i < job_div; ++i) {
                auto offset = i * stride;

                if(i == job_div - 1) {
                    stride = stride + rem;
                }

                _task_queue.push_back(
                    task{
                        .callback = [data_span = std::span < data_type > (data.data() + offset, stride), task_fn]() {
                            task_fn(data_span);
                        },
                        .tk = tk});
            }

            // notify workers
            _cv.notify_all();

            // ----------
            return tk;
        }

        void busy_wait_for (sync_token const & tk) {
            if (tk.done()) {
                return;
            }

            do {
                task current_task;

                {
                    auto lock = std::scoped_lock(_mutex);

                    if (!_task_queue.empty()) {
                        current_task = std::move(_task_queue.front());
                    }
                }

                if (current_task) {
                    current_task.invoke();
                } else {
                    std::this_thread::yield();
                }

            } while (tk.done() == false);
        }

    private:

        std::vector <std::thread>   _workers;
        std::deque<task>            _task_queue;
        spin_mutex                  _mutex;
        std::condition_variable_any _cv;
        std::atomic_bool            _is_running{true};
    };
}

void SEQ_BASELINE(benchmark::State & state) {
    for(auto _: state) {
        auto range = state.range(0);

        for(unsigned i = 0; i < range; ++i) {
            test_data[i].update_matrix();
        }
    }
}

inline void PROTO_F(benchmark::State & state) {
    proto_f::executor exec_f{};

    for(auto _: state) {
        std::span<transformer>::size_type range = state.range(0);

        auto tk = exec_f.run_parallel<transformer>(
            {test_data.data(), range},
            [](std::span<transformer> data) {
                for(auto & trm: data) {
                    trm.update_matrix();
                }
            });

        exec_f.wait_for(tk);
    }
}

inline void PROTO_G(benchmark::State & state) {
    proto_g::executor exec{};

    for(auto _: state) {
        std::span<transformer>::size_type range = state.range(0);

        auto tk = exec.push_parallel<transformer>(
            {test_data.data(), range},
            [](std::span<transformer> data) {
                for(auto & trm: data) {
                    trm.update_matrix();
                }
            });

        exec.busy_wait_for(tk);
    }
}

#define MY_BENCHMARK(x) BENCHMARK(x)->Range(MIN_ITERATION_RANGE, MAX_ITERATION_RANGE)->Unit(benchmark::TimeUnit::kMillisecond)

MY_BENCHMARK(SEQ_BASELINE);
//MY_BENCHMARK(PROTO_F);
MY_BENCHMARK(PROTO_G);

BENCHMARK_MAIN();
