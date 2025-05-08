#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

// defines
#define PAGU_V "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

enum editor_key {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

typedef struct {
    int size;
    char *chars;
} e_row;

typedef struct {
    int cx, cy;
    int screen_rows;
    int screen_cols;
    int n_rows;
    e_row *row;
    struct termios orig_termios;
} editorConfig;

editorConfig E;

// terminal
void enable_raw_mode(void);
void disable_raw_mode(void);
void die(const char *);
int e_read_key();
int get_window_size(int *, int *);
int get_cursor_pos(int *, int *);

// row operations
void e_append_row(char *, size_t);

// file IO
void e_open(char *);

// append buffer
struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void ab_append(struct abuf *ab, const char *s, int len);
void ab_free(struct abuf *ab);

// input
void e_process_keypress();
void e_move_cursor(int);

// output
void e_clear();
void e_draw_rows(struct abuf *);

// init
void e_init();

int main(int argc, char **argv) {
    e_init();
    enable_raw_mode();
    if (argc >= 2) {
        e_open(argv[1]);
    }

    while (1) {
        e_clear();
        e_process_keypress();
    }

    return 0;
}

void enable_raw_mode() {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {
        die("tcgetattr");
    }
    atexit(disable_raw_mode);

    struct termios raw = E.orig_termios;
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
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
        die("tcsetattr");
    }
}

void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

int e_read_key() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) {
            die("read");
        }
    }
    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) {
            return '\x1b';
        }
        if (read(STDIN_FILENO, &seq[1], 1) != 1) {
            return '\x1b';
        }
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                    case '1':
                    case '7':
                        return HOME_KEY;
                    case '3':
                        return DEL_KEY;
                    case '4':
                    case '8':
                        return END_KEY;
                    case '5':
                        return PAGE_UP;
                    case '6':
                        return PAGE_DOWN;
                    }
                }
            } else {
                switch (seq[1]) {
                case 'A':
                    return ARROW_UP;
                case 'B':
                    return ARROW_DOWN;
                case 'C':
                    return ARROW_RIGHT;
                case 'D':
                    return ARROW_LEFT;
                case 'H':
                    return HOME_KEY;
                case 'F':
                    return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
            case 'H':
                return HOME_KEY;
            case 'F':
                return END_KEY;
            }
        }
        return '\x1b';
    } else {
        return c;
    }
}

int get_window_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
            return -1;
        }
        return get_cursor_pos(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

int get_cursor_pos(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
        return -1;
    }

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')
            break;
        i++;
    }

    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;
    return 0;
}

void e_append_row(char *s, size_t len) {
    E.row = realloc(E.row, sizeof(e_row) * (E.n_rows + 1));
    int at = E.n_rows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.n_rows++;
}

// file IO
void e_open(char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        die("fopen");
    }
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 &&
               (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
            linelen--;
        e_append_row(line, linelen);
    }
    free(line);
    fclose(fp);
}

// append buffer
void ab_append(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);
    if (new == NULL)
        return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void ab_free(struct abuf *ab) { free(ab->b); }

// input
void e_process_keypress() {
    int c = e_read_key();
    switch (c) {
    case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;

    case DEL_KEY:
        E.cx--;
        break;

    case HOME_KEY:
        E.cx = 0;
        break;
    case END_KEY:
        E.cx = E.screen_cols - 1;
        break;

    case PAGE_UP:
    case PAGE_DOWN: {
        int times = E.screen_rows;
        while (times--) {
            e_move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
    } break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        e_move_cursor(c);
        break;
    }
}

void e_move_cursor(int key) {
    switch (key) {
    case ARROW_LEFT:
        E.cx -= E.cx - 1 < 0 ? 0 : 1;
        break;
    case ARROW_RIGHT:
        E.cx += E.cx + 1 > E.screen_cols ? 0 : 1;
        break;
    case ARROW_UP:
        E.cy -= E.cy - 1 < 0 ? 0 : 1;
        break;
    case ARROW_DOWN:
        E.cy += E.cy + 2 > E.screen_rows ? 0 : 1;
        break;
    }
}

// output
void e_clear() {
    struct abuf ab = ABUF_INIT;
    ab_append(&ab, "\x1b[?25l", 6);
    ab_append(&ab, "\x1b[H", 3);
    e_draw_rows(&ab);
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    ab_append(&ab, buf, strlen(buf));
    ab_append(&ab, "\x1b[?25h", 6);
    write(STDOUT_FILENO, ab.b, ab.len);
    ab_free(&ab);
}

void e_draw_rows(struct abuf *ab) {
    ab_append(ab, "\x1b[K", 3); // k -> erase line
    for (int y = 0; y < E.screen_rows - 1; y++) {
        if (y >= E.n_rows) {
            if (E.n_rows == 0 && y == E.screen_rows / 3) {
                char welcome[80];
                int welcomelen =
                    snprintf(welcome, sizeof(welcome),
                             "pagu editor -- version %s\r\n", PAGU_V);
                if (welcomelen > E.screen_cols) {
                    welcomelen = E.screen_cols;
                }
                int padding = (E.screen_cols - welcomelen) / 2;
                if (padding) {
                    ab_append(ab, "~", 1);
                    padding--;
                }
                while (padding--) {
                    ab_append(ab, " ", 1);
                }
                ab_append(ab, welcome, welcomelen);
            } else {
                ab_append(ab, "~\r\n", 3);
            }
        } else {
            int len = E.row[y].size;
            if (len > E.screen_cols) {
                len = E.screen_cols;
            }
            ab_append(ab, E.row[y].chars, len);
            ab_append(ab, "\r\n", 2);
        }
        ab_append(ab, "\x1b[K", 3);
    }
    ab_append(ab, "~", 1);
}

// init
void e_init() {
    E.cx = 0;
    E.cy = 0;
    E.n_rows = 0;
    E.row = NULL;

    if (get_window_size(&E.screen_rows, &E.screen_cols) == -1) {
        die("get_window_size");
    }
}
