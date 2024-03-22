#include <sys/types.h>
#include <sys/wait.h>
#include <sys/fcntl.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <filesystem>
#include <map>
#include <thread>

struct exec_report {
    std::map < int, std::size_t >   exit_code_count;
    std::size_t                     timed_out_count = 0;
    std::chrono::milliseconds       runtime_accumulator;
};

void dump_report (exec_report const & report, std::size_t iterations) {
    std::cout << "Execution Report:" << std::endl;
    auto success_it =  report.exit_code_count.find(0);
    auto success_count = (success_it != report.exit_code_count.end () ? success_it->second : 0);

    std::cout << "Success: " << success_count << std::endl;
    std::cout << "Timed out: " << report.timed_out_count << std::endl;
    std::cout << "Runtime Avg: " << report.runtime_accumulator.count() / iterations << "ms" << std::endl;
    std::cout << std::endl;

    std::cout << "Exit Codes: " << std::endl;
    for (auto const & [EXIT_CODE, COUNT] : report.exit_code_count) {
        std::cout << "[" << EXIT_CODE << ": " << COUNT << "]" << std::endl;
    }
}

void run_monitor (exec_report & report, pid_t pid, std::chrono::milliseconds const TIMEOUT) {
    int status = 0;
    auto const START_TIME = std::chrono::system_clock::now();

    do {
        auto const WAIT_RES = waitpid(pid, &status, WNOHANG | WUNTRACED);

        if (WAIT_RES == 0) {
            // child is still running
            std::this_thread::yield();
        } else if (WAIT_RES > 0) {
            // child process has exited
            ++report.exit_code_count[status];
            return;
        } else {
            std::cerr
                << "Failed to wait for child process with error: ("
                << WAIT_RES
                << ")"
                << std::string (strerror (errno))
                << std::endl;
            return;
        }

    } while (std::chrono::system_clock::now() - START_TIME < TIMEOUT);

    // timeout
    kill(pid, SIGKILL);
    ++report.timed_out_count;
}

bool run_process (std::filesystem::path const & PATH) {
    // open /dev/null
    int dev_null = ::open("/dev/null", O_WRONLY);

    if (dev_null == -1) {
        std::cerr << "Failed to open /dev/null" << std::endl;
        return false;
    }

    // redirect standard output and standard error to /dev/null
    if (dup2(dev_null, STDOUT_FILENO) == -1 || dup2(dev_null, STDERR_FILENO) == -1) {
        std::cerr << "Failed to redirect output" << std::endl;
        return false;
    }

    // replace child process with new executable
    char * NONE = nullptr;
    return execvp (PATH.c_str(), &NONE) == 0;
}

int main (int const ARG_C, char const ** ARG_V) {
    // get child executable path from command line
    if (ARG_C != 4) {
        std::cerr << "Usage: " << ARG_V[0] << " <executable_path> <iterations> <timeout_ms>" << std::endl;
        return 1;
    }

    std::filesystem::path executable_path;
    std::size_t iterations = 0;
    std::chrono::milliseconds timeout;

    try {
        executable_path = ARG_V[1];
        iterations = std::stoul(ARG_V[2]);
        timeout = std::chrono::milliseconds(std::stoul(ARG_V[3]));
    } catch (std::exception & e) {
        std::cerr << "Invalid arguments: " << e.what() << std::endl;
        return 1;
    }

    if (!std::filesystem::exists(executable_path)) {
        std::cerr << "Executable not found: " << executable_path << std::endl;
        return 1;
    }

    // create child process
    exec_report report;
    auto it_cursor = iterations;

    while (it_cursor > 0) {
        auto const START_TIME = std::chrono::system_clock::now();
        pid_t pid = vfork();

        if (pid < 0) {
            std::cerr << "Failed to fork process with error:" << std::to_string(pid) << std::endl;
            return 1;
        }

        if (pid == 0) {
            // child process
            if (!run_process(executable_path)) {
                return -1;
            }

            return 0;
        }

        // parent process
        run_monitor(report, pid, timeout);
        report.runtime_accumulator += std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - START_TIME);
        --it_cursor;
    }

    dump_report(report, iterations);

    return 0;
}