/**** includes ****/

#include<stdio.h>
#include<termios.h> // tcgetattr(), tcsetattr()
#include<unistd.h> // read(), write()
#include<stdlib.h> // atexit(), exit()
#include<ctype.h> // iscntrl()
#include<stdio.h> // printf(), perror(), sscanf()
#include<errno.h> // errno, EAGAIN
#include<sys/ioctl.h> // (input/output control) ioctl()

/**** defines ****/
// SECTION Define

/* This macro bitwise-AND a character with 00011111,
in C you generally specify bitmasks using hex.
Ctrl strips 5th and 6th bit with whatever keypress is done*/
#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0} // represents empty buffer

/**** data *****/
// SECTION Data

struct editorConfig {
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

struct editorConfig E;


/**** terminal *****/
// SECTION Terminal

void die(const char *s) {
    // you must look at the references to get how many bytes each command is.
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawMode() {
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios)) die("tcgetattr");
    atexit(disableRawMode);
    struct termios raw = E.orig_termios;

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

/* wait for one keypress and return it.*/
char editorReadKey() {
    int nread;
    char c;
    // read keypresses from the user
    // read one byte from stdin to var c until no more bytes are present to read
    /* read usually returns the number of bytes read, if nothing read then returns 0 */

    /* In Cygwin, when read() times out it returns -1 with an errno of EAGAIN, 
    instead of just returning 0 like it’s supposed to. 
    To make it work in Cygwin, we won’t treat EAGAIN as an error. */
    while((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if(nread == -1 && errno != EAGAIN) die("read");
    }
    return c;
}

/* Called after curser position is called after curser is put to bottom right
corner if ioctl call in getWindowSize() fails.*/
int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
    
    while(i < sizeof(buf) -1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';
    
    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
    return 0;
    
}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;
    // TIOCGWINSZ: Terminal IOCtl Get WINdow SiZe.
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // Put cursor position at the right bottom corner to get terminal size if
        // ioctl call fails to get the information.
        if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    }
    else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
    
}

/**** append buffer ****/
// SECTION buffer

/* Instead of small multiple write() for tilde, we should create a buffer
 and put all tilde there, then write it at once. */

 struct abuf{
     char *b; // points to a buffer in memory
     int len; // for ^this memory length
 };


/**** output ****/
// SECTION Output


void editorDrawRows() {
    int y;
    for(y = 0; y < E.screenrows; y++) {
        /*to print newline, we have to use \r\n in this code.*/

        write(STDOUT_FILENO, "~", 1);
        
        if(y < E.screenrows -1 )
            write(STDOUT_FILENO, "\r\n", 2);

        //[OBSOLETE - last line was not having tilde] write(STDOUT_FILENO, "~\r\n", 3);
    }
}

void editorRefreshScreen() {
    /* 4 means writing 4 bytes to terminal
    \x1b is the first byte, it's an escape char or 27 is decimal
    [2J are other 3 bytes. J command means Erase In Display with 2 as argument
    means clear entire screen.
    Instead of ncurses library, VT100 escape sequences are used here.*/
    write(STDOUT_FILENO, "\x1b[2J", 4);
    /* 3 bytes long escape sequence
    H command to set cursor position. <esc>[row;columnH*/
    write(STDOUT_FILENO, "\x1b[H", 3);

    /* draw tildes till the terminal end and reposition cursor back to text*/
    editorDrawRows();
    write(STDOUT_FILENO, "\x1b[H", 3);
}

/**** input ****/
// SECTION Input

/* waits for keypress and then handles it. */
void editorProcessKeypress() {
    char c = editorReadKey();
    
    // exit on Ctrl+q
    switch(c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
        break;
    }
}


/**** init *****/
// SECTION Init

// Initialize all E struct fields
void initEditor() {
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main() {
    enableRawMode();
    initEditor();

    // repeatedly read() for input wrt VMIN and VTIME.
    while(1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}