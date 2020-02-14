/**** includes ****/

#include<stdio.h>
#include<termios.h> // tcgetattr(), tcsetattr()
#include<unistd.h> // read()
#include<stdlib.h> // atexit(), exit()
#include<ctype.h> // iscntrl()
#include<stdio.h> // printf(), perror()
#include<errno.h> // errno, EAGAIN

/**** data *****/

struct termios orig_termios;


/**** terminal *****/

void die(const char *s) {
    perror(s);
    exit(1);
}

void disableRawMode() {
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &orig_termios)) die("tcgetattr");
    atexit(disableRawMode);
    struct termios raw = orig_termios;

    // ICRNL: CR means carriage return; NL means new line
    // Ctrl+m and Enter key is read as 13 now.
    // disable ctrl+p and ctrl+q which toggle transmission of data to terminal
    // IXON is an input flag
    /*
    BRKINT | INPCK | ISTRIP are either already turned off or are obsolete.
    but turning off these flags is a part of raw mode.
    BRKINT: sends SIGINT on break condition of program
    INPCK: enables parity checking
    */
    raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON | ICRNL);
    /* stop printing every keypress to the terminal 
    and turn off canonical mode. enter raw mode to read input byte by byte
    instead of line by line.*/
    // ISIG to disable SIGINT and SIGSTP
    // IEXTEN : disable Ctrl+v and ctrl+o
    // all the above are local flags (lflag)
    raw.c_lflag &= ~(ECHO | ICANON | ISIG); // negate the echo and canonical flag
    // disable "\n" "\r" POST-processing of Output
    raw.c_oflag &= ~(OPOST);

    /* Turn off more flags
    CS8: not a flag. bit mask with multiple bits. set using bitwise-OR
    It sets character size to 8 bits per byte. 
    */
    raw.c_cflag |= (CS8);

    /*
    timeout for read(), it returns if it doesn't get any input
    VMIN and VTIME are indexes on c_cc field. cc: control characters,
    an array of bytes that control various terminal settings.
    */
    raw.c_cc[VMIN] = 0; // minimum no. of bytes of input needed before read() can return
    raw.c_cc[VTIME] = 1; // max time to wait before read() returns

    if(tcsetattr(STDIN_FILENO,TCSAFLUSH, &raw) == -1) die("tcsetattr");
}


/**** init *****/

int main() {
    enableRawMode();
    // repeatedly read() for input wrt VMIN and VTIME.
    while(1) {
        char c = '\0';
        // read keypresses from the user
        // read one byte from stdin to var c until no more bytes are present to read
        /* read usually returns the number of bytes read, if nothing read then returns 0 */

        /* In Cygwin, when read() times out it returns -1 with an errno of EAGAIN, 
        instead of just returning 0 like it’s supposed to. 
        To make it work in Cygwin, we won’t treat EAGAIN as an error. */
        if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read");
        /* check if 'c' is control character
        control char: nonprintable char (check ASCII table)*/
        if(iscntrl(c))
        /* for newline we have to use \r\n */
            printf("%d\r\n", c);
        else
            printf("%d ('%c')\r\n", c, c);
        // exit on 'q' press
        if (c == 'q') break;
    }
    return 0;
}