#include <atomic>
#include <benchmark/benchmark.h>
#include <memory>
#include <stdexcept>
#include <thread>

#include <las/las.h>

namespace gc {

	enum struct tracking_state : uint8_t {
		active,
		marked,
		unreachable
	};

	using dctor_callback = void(*)(void *);

	struct reference {
		reference*						next { nullptr };
		std::atomic < struct object* >	to { nullptr };
		std::atomic < tracking_state >	state { tracking_state::active };
	};

	struct object {
		object*							next { nullptr };
		object*							next_marked { nullptr };
		void *							payload { nullptr };
		dctor_callback					dctor { nullptr };
		std::atomic < tracking_state >	state { tracking_state::active };
		std::atomic < reference* >		ref_head { nullptr };
	};

	template < typename t >
	void list_push_front (std::atomic < t* > & head, t * node, t * t::*next_field_addr = &t::next) {
		// lockfree push front
		node->*next_field_addr = head.load();

		while (head.compare_exchange_weak(
			node->*next_field_addr,
			node,
			std::memory_order_release,
			std::memory_order_relaxed) == false)
		{ /* spin */ }
	}

	template < typename t >
	t * list_pop_front (std::atomic < t* > & head, t * t::*next_field_addr = &t::next) {
		// lockfree pop front
		auto * old_head = head.load();

		while (old_head != nullptr && head.compare_exchange_weak(
			old_head,
			old_head->*next_field_addr,
			std::memory_order_release,
			std::memory_order_relaxed) == false)
		{ /* spin */ }

		if (old_head != nullptr) {
			old_head->*next_field_addr = nullptr;
		}

		return old_head;
	}

	template < typename t >
	void list_push_front (t * & head, t * node, t * t::*next_field_addr = &t::next) {
		node->*next_field_addr = head;
		head = node;
	}

	template < typename t >
	t * list_pop_front (t * & head, t * t::*next_field_addr = &t::next) {
		if (head != nullptr) {
			auto * old_head = head;
			head = head->*next_field_addr;

			old_head->*next_field_addr = nullptr;
			return old_head;
		}

		return nullptr;
	}

	template < typename t >
	t * list_detach (std::atomic < t * > & head) {
		auto * detached_head = head.load();

		while (head.compare_exchange_weak(
			detached_head,
			nullptr,
			std::memory_order_release,
			std::memory_order_relaxed) == false)
		{ /* spin */ }

		return detached_head;
	}

	template < typename t, typename release_f >
	void list_collect (std::atomic < t * > & head, release_f && release, bool check_state = true) {

		// detach the list
		auto * detached_head = list_detach (head);

		// traverse the list
		while (detached_head != nullptr) {
			auto * next_ptr = detached_head->next;

			if (check_state == false || detached_head->state == tracking_state::unreachable) {
				// remove the object
				release (detached_head);
			} else if (detached_head->state == tracking_state::active){
				// push the object back to the list
				list_push_front (head, detached_head);
			} else {
				// something really bad happened
				throw std::runtime_error ("object marked for collection is still reachable");
			}

			detached_head = next_ptr;
		}
	}

	struct tracker {

		static tracker & instance ();

		void register_collector_thread(std::thread::id id = std::this_thread::get_id()) {
			_collector_thread_id = id;
		}

		gc::reference * reference(object* to);

		static void reference (gc::reference* ref, object* to);
		static void dereference (gc::reference* ref);

		template < typename t, typename ... args_t >
		object * construct(args_t && ... args) {
			return register_object (
				new t (std::forward < args_t > (args)...),
				[](void * payload) {
					delete static_cast < t * > (payload);
				});
		}

		void collect();

	private:

		object * register_object (void * payload, dctor_callback dctor);

		object * swap_object_stack_head(object * obj);
		object * object_stack_head();

		std::atomic < object* >		_objects { nullptr };

		thread_local static object* _object_stack_head;
		object						_object_stack_root {};
		std::thread::id				_collector_thread_id;
	};

	thread_local object * tracker::_object_stack_head = nullptr;

	tracker & tracker::instance() {
		static tracker _instance;
		return _instance;
	}

	gc::reference * tracker::reference(object* to) {
		// mark object as active, since we are referencing it
		if (to != nullptr) {
			to->state.store (tracking_state::active);
		}

		// get the current stack head object
		auto * from = object_stack_head();

		// create reference instance
		auto * new_ref = new gc::reference {
			nullptr,
			to,
			tracking_state::active,
		};

		// push to the reference list of the active object
		list_push_front(from->ref_head, new_ref);

		return new_ref;
	}

	void tracker::reference(gc::reference * ref, object * to) {
		if (to != nullptr) {
			to->state = tracking_state::active;
		}

		ref->to.store (to);
	}

	void tracker::dereference(gc::reference* ref) {
		ref->state = tracking_state::unreachable;
	}

	void tracker::collect() {
		if (std::this_thread::get_id() != _collector_thread_id) {
			throw std::runtime_error ("unexpected thread");
		}

		// trace objects
		{
			// mark all objects as unreachable
			for (auto * obj = _objects.load(); obj != nullptr; obj = obj->next) {
				obj->state.store (tracking_state::unreachable, std::memory_order_release);
			}

			// special case for the root object
			_object_stack_root.state.store (tracking_state::marked, std::memory_order_release);

			// mark all objects reachable from the root as active as we can reach them
			object * next_mark_list = &_object_stack_root;

			while (next_mark_list != nullptr) {
				object* mark_list = next_mark_list;

				next_mark_list = nullptr;

				while (mark_list != nullptr) {

					// clean up the reference list (TODO: perhaps this should be done on its own "stage")
					list_collect (mark_list->ref_head,
						[](struct reference * ref) -> void {
							delete ref;
						});

					for (auto* obj_ref = mark_list->ref_head.load(); obj_ref != nullptr; obj_ref = obj_ref->next) {
						auto* obj = obj_ref->to.load();

						// skip null or disposed references
						if (obj == nullptr || obj_ref->state == tracking_state::unreachable) {
							continue;
						}

						if (obj->state == tracking_state::unreachable) {
							obj->state.store(tracking_state::marked, std::memory_order_release);

							// push to marked objects
							list_push_front (next_mark_list, obj, &object::next_marked);
						}
					}

					// maintain integrity of the list by popping the head
					mark_list->state.store (tracking_state::active, std::memory_order_release);
					list_pop_front (mark_list, &object::next_marked);
				}
			}
		}

		// take the garbage out
		{
			// collect all unreachable objects
			list_collect (
				_objects,
				[](object* obj) -> void {
					// release object references
					list_collect (
						obj->ref_head,
						[](gc::reference * ref) -> void {
							delete ref;
						},
						false);

					// call the destructor and release the managed memory
					obj->dctor (obj->payload);
					delete obj;
				});
		}
	}

	object * tracker::swap_object_stack_head(object * obj) {
		// get the old head
		auto * old_head = this->object_stack_head();

		// replace the head with the new instance
		_object_stack_head = obj;

		return old_head;;
	}

	object * tracker::object_stack_head() {
		if (_object_stack_head == nullptr) {
			_object_stack_head = &_object_stack_root;
		}

		return _object_stack_head;
	}

	object * tracker::register_object(void * payload, dctor_callback dctor) {

		auto * obj = new object {
			nullptr,
			nullptr,
			payload,
			dctor,
			tracking_state::active,
			nullptr
		};

		// set the stack line to mantain reference consistency
		auto* prev_stack_line = swap_object_stack_head (obj);

		// push the object to the list
		list_push_front (_objects, obj);

		// restore the stack line
		swap_object_stack_head (prev_stack_line);

		return obj;
	}

	template < typename _t >
	struct gc_ptr {
	public:

		// -- constructors --
		constexpr gc_ptr() = default;

		constexpr explicit gc_ptr(std::nullptr_t) : gc_ptr() {}

		gc_ptr(const gc_ptr& x) noexcept {
			copy(x);
		}

		template <typename _u>
		explicit gc_ptr(const gc_ptr<_u>& x) noexcept {
			copy(x);
		}

		// -- destructor --
		~gc_ptr() {
			tracker::dereference(_ref);
		}

		// -- operators --
		gc_ptr& operator = (const gc_ptr& other) noexcept {
			if (&other == this) return *this;

			copy(other);
			return *this;
		}

		template <typename _u>
		gc_ptr& operator = (const gc_ptr<_u>& other) noexcept {
			copy(other);
			return *this;
		}

		_t& operator*() const noexcept {
			return *operator->();
		}

		_t* operator->() const noexcept {
			return _ptr;
		}

		explicit operator bool() const noexcept {
			return _ptr != nullptr;
		}

		void reset() noexcept {
			_ptr = nullptr;
			_ref->to.store (nullptr);
		}

		template<typename, typename ... _args_tv >
		friend auto make_gc(_args_tv &&...);

		template <typename>
		friend struct gc_ptr;

	private:

		template < typename _u >
		void copy(gc_ptr <_u> const& other) {
			_ptr = other._ptr;
			tracker::reference (_ref, other._ref->to.load());
		}

		explicit gc_ptr(object* obj) :
			_ref{ tracker::instance().reference(obj) },
			_ptr{ reinterpret_cast <_t*> (obj->payload) }
		{}

		reference * _ref { tracker::instance().reference(nullptr) };
		_t * _ptr{ nullptr };
	};

	template < typename _t, typename ... _args_tv >
	auto make_gc(_args_tv&& ... args) {
		return gc::gc_ptr < _t > (
			gc::tracker::instance().construct < _t, _args_tv... >(std::forward < _args_tv > (args)...));
	}
}

namespace gc2 {

	enum struct tracking_state : uint8_t {
		active,
		marked,
		unreachable
	};

	using dctor_callback_t = void(*)(void *);

	struct reference {
		reference*						next { nullptr };
		std::atomic < struct object* >	to { nullptr };
		std::atomic < tracking_state >	state { tracking_state::active };
	};

	struct object {
		object*							next { nullptr };
		object*							next_marked { nullptr };
		void *							payload { nullptr };
		dctor_callback_t				dctor { nullptr };
		std::atomic < reference* >		ref_head { nullptr };
		std::atomic < tracking_state >	state { tracking_state::active };
	};

	template < typename t, typename release_f >
	void atomic_collect (std::atomic < t * > & head, release_f && release, bool check_state = true) {

		// detach the list
		auto * detached_head = las::atomic_details::atomic_detach (head);

		// traverse the list
		while (detached_head != nullptr) {
			auto * next_ptr = detached_head->next;

			if (check_state == false || detached_head->state == tracking_state::unreachable) {
				// remove the object
				release (detached_head);
			} else if (detached_head->state == tracking_state::active){
				// push the object back to the list
				las::atomic_details::atomic_push (head, detached_head);
			} else {
				// something really bad happened
				throw std::runtime_error ("object marked for collection is still reachable");
			}

			detached_head = next_ptr;
		}
	}

	struct tracker {

		static tracker & instance ();

		void register_collector_thread(std::thread::id id = std::this_thread::get_id()) {
			_collector_thread_id = id;
		}

		gc2::reference * reference(object* to);

		static void reference (gc2::reference* ref, object* to);
		static void dereference (gc2::reference* ref);

		template < typename t, typename ... args_t >
		object * construct(args_t && ... args) {
			return register_object (
				new t (std::forward < args_t > (args)...),
				[](void * payload) {
					delete static_cast < t * > (payload);
				});
		}

		void collect();

	private:

		object * register_object (void * payload, dctor_callback_t dctor);

		object * swap_object_stack_head(object * obj);
		object * object_stack_head();

		std::atomic < object* >		_objects { nullptr };

		thread_local static object* _object_stack_head;
		object						_object_stack_root {};
		std::thread::id				_collector_thread_id;
	};

	thread_local object * tracker::_object_stack_head = nullptr;

	tracker & tracker::instance() {
		static tracker _instance;
		return _instance;
	}

	gc2::reference * tracker::reference(object* to) {
		// mark object as active, since we are referencing it
		if (to != nullptr) {
			to->state.store (tracking_state::active);
		}

		// get the current stack head object
		auto * from = object_stack_head();

		// create reference instance
		auto * new_ref = new gc2::reference {
			nullptr,
			to,
			tracking_state::active,
		};

		// push to the reference list of the active object
		las::atomic_details::atomic_push (from->ref_head, new_ref);

		return new_ref;
	}

	void tracker::reference(gc2::reference * ref, object * to) {
		if (to != nullptr) {
			to->state = tracking_state::active;
		}

		ref->to.store (to);
	}

	void tracker::dereference(gc2::reference* ref) {
		ref->state = tracking_state::unreachable;
	}

	void tracker::collect() {
		if (std::this_thread::get_id() != _collector_thread_id) {
			throw std::runtime_error ("unexpected thread");
		}

		// trace objects
		{
			// mark all objects as unreachable
			for (auto * obj = _objects.load(std::memory_order_relaxed); obj != nullptr; obj = obj->next) {
				obj->state.store (tracking_state::unreachable, std::memory_order_release);

				// clean up the reference list while we are at it
				atomic_collect (obj->ref_head,
					[](struct reference * ref) -> void {
						delete ref;
					});
			}

			// special case for the root object
			_object_stack_root.state.store (tracking_state::marked, std::memory_order_release);

			// mark all objects reachable from the root as active as we can reach them
			object * next_mark_list = &_object_stack_root;

			while (next_mark_list != nullptr) {
				// traverse the current mark list and store the next mark list
				object* mark_list = next_mark_list;
				next_mark_list = nullptr;

				while (mark_list != nullptr) {

					for (auto* obj_ref = mark_list->ref_head.load(std::memory_order_relaxed); obj_ref != nullptr; obj_ref = obj_ref->next) {
						auto* obj = obj_ref->to.load();

						// skip null or disposed references
						if (obj == nullptr || obj_ref->state == tracking_state::unreachable) {
							continue;
						}

						if (obj->state == tracking_state::unreachable) {
							obj->state.store(tracking_state::marked, std::memory_order_release);

							// push to marked objects
							obj->next_marked = next_mark_list;
							next_mark_list = obj;
						}
					}

					// mark object as active, next
					mark_list->state.store (tracking_state::active, std::memory_order_release);
					mark_list = mark_list->next_marked;
				}
			}
		}

		// take the garbage out
		{
			// collect all unreachable objects
			atomic_collect (
				_objects,
				[](object* obj) -> void {
					// release object references
					atomic_collect (
						obj->ref_head,
						[](gc2::reference * ref) -> void {
							delete ref;
						},
						false);

					// call the destructor and release the managed memory
					obj->dctor (obj->payload);
					delete obj;
				});
		}
	}

	object * tracker::swap_object_stack_head(object * obj) {
		// get the old head
		auto * old_head = this->object_stack_head();

		// replace the head with the new instance
		_object_stack_head = obj;

		return old_head;;
	}

	object * tracker::object_stack_head() {
		if (_object_stack_head == nullptr) {
			_object_stack_head = &_object_stack_root;
		}

		return _object_stack_head;
	}

	object * tracker::register_object(void * payload, dctor_callback_t dctor) {

		auto * obj = new object {
			nullptr,
			nullptr,
			payload,
			dctor,
			nullptr,
			tracking_state::active,
		};

		// set the stack line to mantain reference consistency
		auto* prev_stack_line = swap_object_stack_head (obj);

		// push the object to the list
		las::atomic_details::atomic_push (_objects, obj);

		// restore the stack line
		swap_object_stack_head (prev_stack_line);

		return obj;
	}

	template < typename _t >
	struct gc_ptr {
	public:

		// -- constructors --
		constexpr gc_ptr() = default;

		constexpr explicit gc_ptr(std::nullptr_t) : gc_ptr() {}

		gc_ptr(const gc_ptr& x) noexcept {
			copy(x);
		}

		template <typename _u>
		explicit gc_ptr(const gc_ptr<_u>& x) noexcept {
			copy(x);
		}

		// -- destructor --
		~gc_ptr() {
			tracker::dereference(_ref);
		}

		// -- operators --
		gc_ptr& operator = (const gc_ptr& other) noexcept {
			if (&other == this) return *this;

			copy(other);
			return *this;
		}

		template <typename _u>
		gc_ptr& operator = (const gc_ptr<_u>& other) noexcept {
			copy(other);
			return *this;
		}

		_t& operator*() const noexcept {
			return *operator->();
		}

		_t* operator->() const noexcept {
			return _ptr;
		}

		explicit operator bool() const noexcept {
			return _ptr != nullptr;
		}

		void reset() noexcept {
			_ptr = nullptr;
			_ref->to.store (nullptr);
		}

		template<typename, typename ... _args_tv >
		friend auto make_gc(_args_tv &&...);

		template <typename>
		friend struct gc_ptr;

	private:

		template < typename _u >
		void copy(gc_ptr <_u> const& other) {
			_ptr = other._ptr;
			tracker::reference (_ref, other._ref->to.load());
		}

		explicit gc_ptr(object* obj) :
			_ref{ tracker::instance().reference(obj) },
			_ptr{ reinterpret_cast <_t*> (obj->payload) }
		{}

		reference * _ref { tracker::instance().reference(nullptr) };
		_t * _ptr{ nullptr };
	};

	template < typename _t, typename ... _args_tv >
	auto make_gc(_args_tv&& ... args) {
		return gc2::gc_ptr < _t > (
			gc2::tracker::instance().construct < _t, _args_tv... >(std::forward < _args_tv > (args)...));
	}
}

constexpr uint32_t object_size = 16;//1_kb;

struct demo {
	uint8_t xxx[object_size];
	gc::gc_ptr <demo> to;
};

void gc_alloc_assign(benchmark::State &state) {
	gc::tracker::instance().register_collector_thread();

	auto root = gc::make_gc<demo>();
	auto node = root;

	for (auto _ : state) {

		for (std::size_t i = 0; i < state.range(0); ++i) {
			node->to = gc::make_gc < demo > ();
		}

		state.PauseTiming();
		gc::tracker::instance().collect();
		state.ResumeTiming();
	}
}

void gc_collect(benchmark::State &state) {
	gc::tracker::instance().register_collector_thread();

	auto root = gc::make_gc<demo>();
	auto node = root;

	for (auto _ : state) {
		state.PauseTiming();

		for (std::size_t i = 0; i < state.range(0); ++i) {
			node->to = gc::make_gc < demo > ();
		}

		state.ResumeTiming();

		gc::tracker::instance().collect();
	}
}

struct no_gc_demo {
	uint8_t xxx[object_size];
	no_gc_demo *cenas;
};

void no_gc_baseline_alloc(benchmark::State &state) {

	std::vector<std::unique_ptr<no_gc_demo> > recovery;

	for (auto _ : state) {
		auto root = new no_gc_demo();

		for (std::size_t i = 0; i < state.range(0); ++i) {
			auto obj = new no_gc_demo();
			obj->cenas = root;
			root->cenas = obj;

			state.PauseTiming();
			recovery.emplace_back(obj);
			state.ResumeTiming();
		}

		state.PauseTiming();
		recovery.clear();
		state.ResumeTiming();
	}
}

void no_gc_baseline_collect(benchmark::State &state) {

	std::vector<std::unique_ptr<no_gc_demo> > recovery;

	for (auto _ : state) {
		auto root = new no_gc_demo();

			state.PauseTiming();
		for (std::size_t i = 0; i < state.range(0); ++i) {
			auto obj = new no_gc_demo();
			obj->cenas = root;
			root->cenas = obj;

			recovery.emplace_back(obj);
		}
		state.ResumeTiming();

		recovery.clear();
	}
}

void shared_ptr_alloc_baseline(benchmark::State &state) {

	std::vector<std::shared_ptr<no_gc_demo> > recovery;

	for (auto _ : state) {
		auto root = std::make_shared<no_gc_demo>();

		for (std::size_t i = 0; i < state.range(0); ++i) {
			auto obj = std::make_shared<no_gc_demo>();
			obj->cenas = root.get();
			root->cenas = obj.get();

			state.PauseTiming();
			recovery.emplace_back(obj);
			state.ResumeTiming();
		}

		state.PauseTiming();
		recovery.clear();
		state.ResumeTiming();
	}
}

void shared_ptr_collect_baseline(benchmark::State &state) {

	std::vector<std::shared_ptr<no_gc_demo> > recovery;

	for (auto _ : state) {
		state.PauseTiming();
		auto root = std::make_shared<no_gc_demo>();

		for (std::size_t i = 0; i < state.range(0); ++i) {
			auto obj = std::make_shared<no_gc_demo>();
			obj->cenas = root.get();
			root->cenas = obj.get();

			state.PauseTiming();
			recovery.emplace_back(obj);
			state.ResumeTiming();
		}
		state.ResumeTiming();

		recovery.clear();
	}
}

struct demo2 {
	uint8_t xxx[object_size];
	gc2::gc_ptr <demo2> to;
};

void gc2_alloc_assign(benchmark::State &state) {
	gc2::tracker::instance().register_collector_thread();

	auto root = gc2::make_gc<demo2>();
	auto node = root;

	for (auto _ : state) {

		for (std::size_t i = 0; i < state.range(0); ++i) {
			node->to = gc2::make_gc < demo2 > ();
		}

		state.PauseTiming();
		gc2::tracker::instance().collect();
		state.ResumeTiming();
	}
}

void gc2_collect(benchmark::State &state) {
	gc2::tracker::instance().register_collector_thread();

	auto root = gc2::make_gc<demo2>();
	auto node = root;

	for (auto _ : state) {
		state.PauseTiming();

		for (std::size_t i = 0; i < state.range(0); ++i) {
			node->to = gc2::make_gc < demo2 > ();
		}

		state.ResumeTiming();

		gc2::tracker::instance().collect();
	}
}

#define GC_BENCHMARK(func) BENCHMARK(func)->Range(1 << 8, 1 << 18)->Unit(benchmark::TimeUnit::kMillisecond)
//GC_BENCHMARK(no_gc_baseline_alloc);
//GC_BENCHMARK(no_gc_baseline_collect);
//GC_BENCHMARK(shared_ptr_alloc_baseline);
//GC_BENCHMARK(shared_ptr_collect_baseline);
GC_BENCHMARK(gc_alloc_assign);
GC_BENCHMARK(gc_collect);
//GC_BENCHMARK(gc2_alloc_assign);
//GC_BENCHMARK(gc2_collect);
