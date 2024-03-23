#include <lockfree_stack.h>
#include <ptbench.h>

int main () {

    lockfree_stack < uint_fast32_t > stack;

    ptbench::executor exec {};

    for (int i = 10; i < 2000; ++i) {
        exec.dispatch ({
                { [&]{ stack.push_back (ptbench::uniform (1000)); }, 50 },
                { [&]{ stack.pop_back (); }, 50 }
            },
            i);
    }

    return 0;
}