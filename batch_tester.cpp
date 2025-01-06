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

#include <cxxopts.hpp>

struct exec_report {
    std::map < int, std::size_t >   exit_code_count;
    std::size_t                     timed_out_count = 0;
    std::chrono::milliseconds       it_runtime_accumulator {};
    std::chrono::milliseconds       runtime {};
};

struct options {
    static void print_help () {
        std::cout << make_parser().help () << std::endl;
    }

    static options parse (int arg_c, char ** arg_v) {
        auto const opts = make_parser ().parse (arg_c, arg_v);

        if (opts.count ("command") == 0) {
            throw std::invalid_argument ("Target command missing");
        }

        return {
            /* ARGS       */ (opts.count ("args") != 0 ? opts ["args"].as<std::vector <std::string>>() : std::vector <std::string> ()),
            /* COMMAND    */ opts ["command"].as<std::string>(),
            /* TIMEOUT    */ std::chrono::milliseconds(opts ["timeout"].as < uint32_t >()),
            /* ITERATIONS */ opts ["iterations"].as < uint32_t > (),
            /* SHOW HELP  */ opts ["help"].as<bool>(),
        };
    }

    std::vector < std::string > args;
    std::string                 command;
    std::chrono::milliseconds   timeout;
    uint32_t                    iterations;
    bool                        show_help;
    bool                        verbose;

private:
    static cxxopts::Options make_parser () {
        auto parser = cxxopts::Options("Batch Tester", "Batch runs a command tracking failure/success rates");

        parser.positional_help ("command [args]...");
        parser.show_positional_help ();

        parser.add_options()
            ("i,iterations", "Number of times to execute the targeted command", cxxopts::value <uint32_t>()->default_value("1000"))
            ("t,timeout", "Maximum time an interation is allowed to run, in milliseconds", cxxopts::value<uint32_t>()->default_value("10"))
            ("v,verbose", "Disable redirection of standard output and standard error")
            ("command", "Targeted command", cxxopts::value < std::string > ())
            ("args", "Targeted command arguments", cxxopts::value < std::vector < std::string > > ());

        parser.add_options()
            ("h,help", "Print Help");

        parser.parse_positional ({"command", "args"});

        return parser;
    }
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
    auto const success_it =  report.exit_code_count.find(0);
    auto const success_count = (success_it != report.exit_code_count.end () ? success_it->second : 0);

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

bool eval_command(std::string const& command) {
    return std::filesystem::exists(command);
}

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

bool eval_command (std::string const & command) {
    return access(command.c_str(), X_OK) == 0;
}

std::optional < process_handle_t > run_process (options const & opts) {
    // create native args
    auto native_args = std::make_unique < char * [] > (opts.args.size() + 1);

    for (auto i = 0; i < opts.args.size(); ++i) {
        native_args [i] = const_cast < char * > (opts.args[i].c_str());
    }

    native_args[opts.args.size()]         = nullptr;
    constexpr char * const no_env[1] = {nullptr};

    // -- vfork -- avoid stack changes after this point -----------------------------------------
    auto pid = vfork();

    if (pid < 0) {
        std::cerr << "Failed to fork process with error:" << std::to_string(pid) << std::endl;
        return std::nullopt;
    }

    if (pid > 0) {
        // parent process
        return pid;
    }

    if (!opts.verbose) {
        // child process
        // redirect process output to /dev/null
        auto const dev_null = ::open("/dev/null", O_WRONLY);

        if (dev_null == -1) {
            std::cerr << "Failed to open /dev/null" << std::endl;
            return std::nullopt;
        }

        // redirect standard output and standard error to /dev/null
        if (dup2(dev_null, STDOUT_FILENO) == -1 || dup2(dev_null, STDERR_FILENO) == -1) {
            std::cerr << "Failed to redirect output" << std::endl;
            return std::nullopt;
        }
    }

    // replace child process with new executable
    execve (opts.command.c_str (), native_args.get(), no_env);

    // failure to replace process with executable
    // exit without calling destructors and others to avoid stack corruption on the parent process
    _exit (1);
}

#endif

int main (int arg_c, char ** arg_v) {

    options opts;

    try {
        opts = options::parse (arg_c, arg_v);
    } catch (std::exception & ex) {
        std::cout << "Invalid command line arguments: " << ex.what ()
            << std::endl
            << std::endl;

        options::print_help ();
        return 0;
    }

    if (opts.show_help) {
        options::print_help ();
        return 0;
    }

    if (!eval_command(opts.command)) {
        std::cerr << "Command \"" << opts.command << "\" is not valid or not executable" << std::endl;
    }

    auto const START_TIME = std::chrono::system_clock::now();

    // create child process
    exec_report report;
    auto it_cursor = opts.iterations;

    while (it_cursor > 0) {
        auto const START_IT_TIME = std::chrono::system_clock::now();

        // parent process
        auto opt_pid = run_process(opts);
        
        if (!opt_pid) {
            // run process failed, better bail out
            return 1;
        }

        run_monitor(opt_pid.value(), report, opts.timeout);

        report.it_runtime_accumulator += std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - START_IT_TIME);
        --it_cursor;
    }

    report.runtime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - START_TIME);
    dump_report(report, opts.iterations);

    return 0;
}