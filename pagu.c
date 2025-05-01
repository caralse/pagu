#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)

struct termios orig_termios;

// terminal
void enable_raw_mode(void);
void disable_raw_mode(void);
void die(const char *);
char e_read_key();

// init
void e_process_keypress();

// output
void e_clear();
void e_draw_rows();

int main() {
    enable_raw_mode();

    while (1) {
        e_clear();
        e_process_keypress();
    }

    return 0;
}

void enable_raw_mode() {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        die("tcgetattr");
    }
    atexit(disable_raw_mode);

    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | IEXTEN | ICANON | ISIG);

    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        die("tcsetattr");
    }
}

void disable_raw_mode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) {
        die("tcsetattr");
    }
}

void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

char e_read_key() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }
    return c;
}

/*** input ***/
void e_process_keypress() {
    char c = e_read_key();
    switch (c) {
    case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;
    }
}

/*** output ***/
void e_clear() {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    e_draw_rows();
    write(STDOUT_FILENO, "\x1b[H", 3);
}

void e_draw_rows() {
    int y;
    for (y = 0; y < 24; y++) {
        write(STDOUT_FILENO, "~\r\n", 3);
    }
}
