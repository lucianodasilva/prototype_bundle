#if defined (_WIN32) || defined (_WIN64)
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#else
#   include <sys/types.h>
#   include <sys/wait.h>
#   include <sys/fcntl.h>
#endif

#include <chrono>
#include <cstring>
#include <iostream>
#include <filesystem>
#include <map>
#include <thread>
#include <optional>

struct exec_report {
    std::map < int, std::size_t >   exit_code_count;
    std::size_t                     timed_out_count = 0;
    std::chrono::milliseconds       it_runtime_accumulator {};
    std::chrono::milliseconds       runtime {};
};

std::string time_format (std::chrono::milliseconds const TIME) {
    std::stringstream stream;
    auto const HOURS = std::chrono::duration_cast < std::chrono::hours > (TIME).count ();
    auto const MINUTES = std::chrono::duration_cast < std::chrono::minutes > (TIME).count () % 60;
    auto const SECONDS = std::chrono::duration_cast < std::chrono::seconds > (TIME).count () % 60;
    auto const MS = std::chrono::duration_cast < std::chrono::milliseconds > (TIME).count () % 1000;

    stream
        << std::setw (2) << std::setfill ('0') << HOURS << ":"
        << std::setw (2) << std::setfill ('0') << MINUTES << ":"
        << std::setw (2) << std::setfill ('0') << SECONDS << "."
        << std::setw (3) << std::setfill ('0') << MS;

    return stream.str ();
}

void dump_report (exec_report const & report, std::size_t iterations) {
    std::cout << "Execution Report:" << std::endl;
    auto success_it =  report.exit_code_count.find(0);
    auto success_count = (success_it != report.exit_code_count.end () ? success_it->second : 0);

    std::cout << "Success: " << success_count << std::endl;
    std::cout << "Timed out: " << report.timed_out_count << std::endl;
    std::cout << "Avg Iteration Time: " << report.it_runtime_accumulator.count() / iterations << "ms" << std::endl;
    std::cout << "Runtime: " << time_format (report.runtime) << std::endl;

    if (success_count != iterations && report.exit_code_count.size() > 1) {
        std::cout << "== Exit Code Counters == " << std::endl;
        for (auto const & [EXIT_CODE, COUNT] : report.exit_code_count) {
            std::cout << "[" << EXIT_CODE << ": " << COUNT << "]" << std::endl;
        }
    }
}

#if defined (_WIN32) || defined (_WIN64)

using process_handle_t = HANDLE;

std::optional < process_handle_t > run_process(std::filesystem::path const& PATH) {
    STARTUPINFO si{};
    PROCESS_INFORMATION pi{};

	si.cb = sizeof(si);
    
    if (!CreateProcess(
        static_cast < LPCSTR > (PATH.string().c_str()),
        nullptr,
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    )) {
		std::cerr << "Failed to create process with error: " << GetLastError() << std::endl;
		return std::nullopt;
	}

	CloseHandle(pi.hThread);

	return pi.hProcess;
}

void run_monitor(process_handle_t const HANDLE, exec_report& report, std::chrono::milliseconds const TIMEOUT) {
    DWORD waitResult = WaitForSingleObject(HANDLE, TIMEOUT.count());

    if (waitResult == WAIT_TIMEOUT) {
        // Timeout occurred, kill the process
        TerminateProcess(HANDLE, 0);
        ++report.timed_out_count;
    } else if (waitResult == WAIT_OBJECT_0) {
        // Process has exited
        DWORD exitCode;
        GetExitCodeProcess(HANDLE, &exitCode);
        ++report.exit_code_count[exitCode];
    } else {
        // Error occurred
        std::cerr << "Failed to wait for process with error: " << GetLastError() << std::endl;
    }

    CloseHandle(HANDLE);
}

#else
using process_handle_t = pid_t;

void run_monitor (process_handle_t const HANDLE, exec_report & report, std::chrono::milliseconds const TIMEOUT) {
    int status = 0;
    auto const START_TIME = std::chrono::system_clock::now();

    do {
        auto const WAIT_RES = waitpid(HANDLE, &status, WNOHANG | WUNTRACED);

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
    kill(HANDLE, SIGKILL);
    ++report.timed_out_count;
}

std::optional < process_handle_t > run_process (std::filesystem::path const & PATH) {
    pid_t pid = vfork();

    if (pid < 0) {
        std::cerr << "Failed to fork process with error:" << std::to_string(pid) << std::endl;
        return std::nullopt;
    }

    if (pid > 0) {
        // parent process
        //TODO: report pid
        return pid;
    }

    // child process

    // redirect process output to /dev/null
    int dev_null = ::open("/dev/null", O_WRONLY);

    if (dev_null == -1) {
        std::cerr << "Failed to open /dev/null" << std::endl;
        return std::nullopt;
    }

    // redirect standard output and standard error to /dev/null
    if (dup2(dev_null, STDOUT_FILENO) == -1 || dup2(dev_null, STDERR_FILENO) == -1) {
        std::cerr << "Failed to redirect output" << std::endl;
        return std::nullopt;
    }

    // replace child process with new executable
    char * NONE = nullptr;
    execvp (PATH.c_str(), &NONE);
    return std::nullopt;
}

#endif

int main (int const ARG_C, char const ** ARG_V) {
    auto const START_TIME = std::chrono::system_clock::now();

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
        auto const START_IT_TIME = std::chrono::system_clock::now();

        // parent process
        auto opt_pid = run_process(executable_path);
        
        if (!opt_pid) {
            // run process failed, beter bail out
            return 1;
        }

        run_monitor(opt_pid.value(), report, timeout);

        report.it_runtime_accumulator += std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - START_IT_TIME);
        --it_cursor;
    }

    report.runtime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - START_TIME);
    dump_report(report, iterations);

    return 0;
}