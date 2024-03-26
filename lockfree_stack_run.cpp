#include <lockfree_stack.h>
#include <ptbench.h>
#include <iostream>
#include <map>
#include <optional>
#include <string>

enum struct run_duration {
    small,
    medium,
    large
};

bool from_string (std::string_view string_value, run_duration & out) {
    auto match = [&](std::string_view expected, run_duration value) -> bool {
        bool const MATCH = string_value == expected;
        if (MATCH) { out = value; }
        return MATCH;
    };

    return
        match ("small", run_duration::small) ||
        match ("medium", run_duration::medium) ||
        match ("large", run_duration::large);
}

void print_usage (int const ARG_C, char const * const * ARG_V) {
    std::cerr << "Usage: " << ARG_V[0] << " [run_duration]" << std::endl;
    std::cerr << "\trun_duration: Optional. Values: 'small', 'medium', 'large'. Defaults to 'small'" << std::endl;
}

std::optional < std::size_t > map_duration_to_iterations (run_duration duration) {
    static auto map = std::map < run_duration, std::size_t > {
        { run_duration::small, 1000 },
        { run_duration::medium, 10000 },
        { run_duration::large, 1000000 }
    };

    auto const IT = map.find (duration);

    if (IT != map.end ()) {
        return IT->second;
    }

    return std::nullopt;
}

int main (int const ARG_C, char const * const * ARG_V) {

    run_duration duration = run_duration::medium;

    if (ARG_C == 2) {
        if (!from_string (ARG_V[1], duration)) {
            print_usage (ARG_C, ARG_V);
        }
    } else if (ARG_C > 2) {
        print_usage (ARG_C, ARG_V);
        return 1;
    }

    auto const OPT_ITER = map_duration_to_iterations (duration);

    if (!OPT_ITER) {
        std::cerr << "Unknown duration kind!" << std::endl;
        return 1;
    }

    // test infrastructure
    lockfree_stack < uint_fast32_t > stack;
    ptbench::executor exec {};

    for (int i = 10; i < OPT_ITER.value(); ++i) {
        exec.dispatch ({
                { [&]{ stack.push_back (ptbench::uniform (1000)); }, 50 },
                { [&]{ stack.pop_back (); }, 50 },
                {[&]{ stack.clear (); }, 5 },
            },
            i);
    }

    return 0;
}