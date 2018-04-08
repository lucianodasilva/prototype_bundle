#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

int main (void)
{

    unsigned char buff [6];
    struct termios original, raw;

    // alternate buffer screen
    printf("\033[?1049h");
    fflush(stdout);

    // Save original serial communication configuration for stdin
    // tcgetattr( STDIN_FILENO, &original);

    // Put stdin in raw mode so keys get through directly without
    // requiring pressing enter.
    // cfmakeraw (&raw);
    // tcsetattr (STDIN_FILENO, TCSANOW, &raw);

    // find term size
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

    printf("\033[48;5;7;38;5;0m[%d, %d]", w.ws_row, w.ws_col);
    printf("\033[48;5;2m          ");
    fflush(stdout);

    // print colors 256
    /*
    for (int i = 0; i < 256; ++i) {
        printf ("\033[38;5;%im", i);
        printf ("\033[48;5;%im", i);
        printf (" ");
        fflush(stdout);
    }
    printf ("\n");
    */

    //while (1) {
    //    read (STDIN_FILENO, &buff, 1);
    //    if (buff[0] == 3) {
    //        // User pressd Ctr+C
    //        break;
    //    }
    //}

    // restore input mode
    tcsetattr (STDIN_FILENO, TCSANOW, &original);

    getchar();

    printf("\033[?1049l");
    fflush(stdout);

    return 0;

    // Save original serial communication configuration for stdin
    //tcgetattr( STDIN_FILENO, &original);

    // Put stdin in raw mode so keys get through directly without
    // requiring pressing enter.
    // cfmakeraw (&raw);
    // tcsetattr (STDIN_FILENO, TCSANOW, &raw);

    /*
    while (1) {

        read (STDIN_FILENO, &buff, 1);
        if (buff[0] == 3) {
            // User pressd Ctr+C
            break;
        }
    }*/

    // restore standard buffer screen
    //printf("\033[?1049l");

    // restore input mode
    //tcsetattr (STDIN_FILENO, TCSANOW, &original);

    return 0;
    unsigned int x, y, btn;




    // Save original serial communication configuration for stdin
    tcgetattr( STDIN_FILENO, &original);

    // Put stdin in raw mode so keys get through directly without
    // requiring pressing enter.
    cfmakeraw (&raw);
    tcsetattr (STDIN_FILENO, TCSANOW, &raw);

    // Switch to the alternate buffer screen
    write (STDOUT_FILENO, "\e[?47h", 6);

    // Enable mouse tracking
    write (STDOUT_FILENO, "\e[?9h", 5);
    while (1) {
        read (STDIN_FILENO, &buff, 1);
        if (buff[0] == 3) {
            // User pressd Ctr+C
            break;
        } else if (buff[0] == '\x1B') {
            // We assume all escape sequences received 
            // are mouse coordinates
            read (STDIN_FILENO, &buff, 5);
            btn = buff[2] - 32;
            x = buff[3] - 32;
            y = buff[4] - 32;
            printf ("button:%u\n\rx:%u\n\ry:%u\n\n\r", btn, x, y);
        }
    }

    // Revert the terminal back to its original state
    write (STDOUT_FILENO, "\e[?9l", 5);
    write (STDOUT_FILENO, "\e[?47l", 6);
    tcsetattr (STDIN_FILENO, TCSANOW, &original);
    return 0;
}