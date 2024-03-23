#if defined __linux

#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <iostream>

int main (void)
{
    struct termios original, raw;

    // Save original serial communication configuration for stdin
    tcgetattr( STDIN_FILENO, &original);

    // Put stdin in raw mode so keys get through directly without
    // requiring pressing enter.
    cfmakeraw (&raw);
    tcsetattr (STDIN_FILENO, TCSANOW, &raw);

    std::cout << "== begin ==" << std::endl;

    bool running = true;

    while (running) {
        char char_in;
        if (read(STDIN_FILENO, &char_in, 1) == 0)
            char_in = 0;

        if (char_in == '\033')
            running = false;

    }

    // restore input mode
    tcsetattr (STDIN_FILENO, TCSANOW, &original);

    std::cout << "== end ==" << std::endl;

    return 0;
}
#else

int main() {
	return 0;
}

#endif