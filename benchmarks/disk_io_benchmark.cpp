#include <las/las.h>
#include <las/test/test.h>
#include <iostream>
#include <fstream>
#include <optional>
#include <thread>
#include <atomic>
#include <sys/stat.h>
#include <sys/mman.h>

struct proc_mem_info {
    // virtual memory size
    std::size_t vmem;
    // physical memory size
    std::size_t pmem;
};

void clean_screen () {
    auto const ansi_clear_screen = "\033[2J\033[1;1H";
    auto const ansi_cursor_home = "\033[H";

    std::cout << ansi_clear_screen << ansi_cursor_home;
}

#if defined (LAS_OS_GNU_LINUX)
// get process memory information from /proc/self/status
std::optional < proc_mem_info > get_proc_mem_info () {
    proc_mem_info info {};

    std::ifstream ifs("/proc/self/status");

    if (!ifs) {
        return std::nullopt;
    }

    std::string line;
    while (std::getline(ifs, line)) {
        if (line.find("VmSize") != std::string::npos) {
            std::istringstream iss(line);
            std::string key;
            iss >> key >> info.vmem;
        } else if (line.find("VmRSS") != std::string::npos) {
            std::istringstream iss(line);
            std::string key;
            iss >> key >> info.pmem;
        }
    }

    return info;
}
#endif

struct benchmark_monitor {
public:

    void start (std::size_t file_size) {
        memory = 0;
        peak_memory = 0;
        vmemory = 0;
        peak_vmemory = 0;
        read_speed = 0;
        peak_read_speed = 0;
        read_size = 0;
        progress = 0;
	read_acc = 0;
	read_count = 0;
        size_to_read = file_size;

        _job = las::job ([&](las::job_token token) {
            using namespace std::chrono_literals;
            constexpr auto interval = 100ms;
            token.interval_reset(interval);

            while (token.stop_requested() == false) {

                // update memory usage
                auto opt_info = get_proc_mem_info ();

                if (opt_info) {
                    memory = opt_info->pmem;
                    vmemory = opt_info->vmem;

                    if (memory > peak_memory) {
                        peak_memory = memory;
                    }

                    if (vmemory > peak_vmemory) {
                        peak_vmemory = vmemory;
                    }
                }

                // update progress
                progress = (read_size * 100) / size_to_read;

                this->print ();
                token.interval_wait();
            }

            // update progress
            progress = (read_size * 100) / size_to_read;
            this->print ();
        });
    }

    void stop () {
        _job.stop();

        if (_job.joinable()) {
            _job.join();
        }
    }

    void increment_read_size (std::size_t size) {
        using namespace std::chrono;
        auto now = std::chrono::system_clock::now();

        // 1 second in microseconds
        // calculate read speed in seconds from the increment and time since last update
        duration < double, std::ratio < 1 > > fp_duration = now - _last_time;

        read_speed = static_cast < double > (size) * (1.0 / fp_duration.count());
	read_acc += read_speed;

        if (read_speed > peak_read_speed) {
            peak_read_speed = read_speed;
        }

	++read_count;
	read_speed = read_acc / read_count;

        _last_time = std::chrono::system_clock::now();

        read_size.fetch_add(size);


    }

    void print () {
        clean_screen();

        std::cout << "Memory: " << memory << " bytes (Peak: " << peak_memory << " bytes)" << std::endl;
        std::cout << "V Memory: " << vmemory << " bytes (Peak: " << peak_vmemory << " bytes)" << std::endl;
        std::cout << "Read Avr Speed: " << read_speed << " bytes/s (Peak: " << peak_read_speed << " bytes/s)" << std::endl;
        std::cout << "Read Size: " << read_size << " bytes" << std::endl;
        std::cout << "Size to Read: " << size_to_read << " bytes" << std::endl;
        std::cout << "Progress: " << progress << "%" << std::endl;
    }

    // current memory usage
    std::size_t memory;
    // peak memory usage
    std::size_t peak_memory;
    // current virtual memory usage
    std::size_t vmemory;
    // peak virtual memory usage
    std::size_t peak_vmemory;
    // read average speed
    std::atomic_size_t read_speed;
    // peak read speed
    std::size_t peak_read_speed;
    // read data size
    std::atomic_size_t read_size;
    // data size to read
    std::size_t size_to_read;
    // progress
    std::size_t progress;

    std::size_t read_acc;
    std::size_t read_count;

private:
    las::job _job;
    std::chrono::time_point<std::chrono::system_clock> _last_time;
};

std::size_t generate_random_file (std::filesystem::path const & path, std::size_t size) {
    if (exists (path) && std::filesystem::is_regular_file(path) && file_size(path) == size) {
        return size;
    }

    if (exists(path)) {
        std::filesystem::remove(path);
    }

    auto * file = fopen (path.c_str(), "wb");

    // check for file open error
    if (!file) {
        throw std::runtime_error("failed to open file");
    }

    // safeguard file from being leaked
    auto file_guard = las::scope_exit ([&] {
        if (file) {
            fclose(file);
        }
    });

    // read file page size
    auto const page_size = getpagesize();

    // generate random file content
    std::byte buffer[page_size];
    std::size_t actual_size = 0;
    las::test::uniform_generator generator;

    // write random data to file
    for (actual_size = 0; actual_size < size; actual_size += page_size)
    {
        for (std::size_t i = 0; i < page_size; ++i) {
            buffer[i] = static_cast < std::byte > (generator(255));
        }

        fwrite (buffer, page_size, 1, file);
    }

    fflush (file);

    return actual_size;
}

constexpr std::size_t gb_to_bytes (std::size_t size) {
    return size * (1024 * 1024 * 1024);
}

constexpr std::size_t mb_to_bytes (std::size_t size) {
    return size * (1024 * 1024);
}

// random file name
auto const random_file_path = std::filesystem::current_path() / "random_file.bin";
constexpr auto random_file_size = gb_to_bytes(10);

void benchmark_naif_read (std::size_t file_size) {
    benchmark_monitor monitor {};

    auto * file = fopen(random_file_path.c_str(), "rb");

    if (!file) {
        throw std::runtime_error("failed to open file");
    }

    auto const page_size = getpagesize();
    std::byte buffer[page_size];
    std::byte buffer2[page_size];

    monitor.start (file_size); // start
    while (monitor.read_size < file_size) {
        auto read_size = fread(buffer, 1, page_size, file);
        memcpy (buffer2, buffer, read_size);
        monitor.increment_read_size(read_size);
    }
    monitor.stop(); // stop monitoring

    fclose (file);
}

void benchmark_mapped_file (std::size_t file_size) {
    benchmark_monitor monitor {};

    auto * file = fopen(random_file_path.c_str(), "rb");
    auto * mapped = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE | MAP_NORESERVE, fileno(file), 0);

    madvise (mapped, file_size, MADV_SEQUENTIAL);

    auto const page_size = getpagesize();
    std::byte buffer[page_size];

    monitor.start (file_size); // start

    for (std::size_t i = 0; i < file_size; i += page_size) {
        memcpy(buffer, static_cast < std::byte * > (mapped) + i, page_size);
	//madvise(mapped + i - page_size, page_size, MADV_DONTNEED);
        monitor.increment_read_size(page_size);
    }

    monitor.stop(); // stop monitoring
    fclose (file);
}

int main () {
    clean_screen();

    std::cout << "generating random " << random_file_size << " byte file : " << random_file_path << std::endl;
    auto const actual_size = generate_random_file(random_file_path, random_file_size);

    // run naif read benchmark
    // benchmark_naif_read(actual_size);
    benchmark_mapped_file(actual_size);

    return 0;
}
