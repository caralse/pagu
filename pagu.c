#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

// defines
#define PAGU_V "0.0.1"
#define PAGU_TAB_STOP 4
#define PAGU_QUIT_TIMES 1

#define CTRL_KEY(k) ((k) & 0x1f)

#define HL_HIGHLIGHT_NUMBERS (1 << 0)
#define HL_HIGHLIGHT_STRINGS (1 << 1)

enum editor_key {
    BACKSPACE = 127,
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

enum editor_highlight {
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_MLCOMMENT,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH
};

typedef struct {
    int idx;
    int size;
    int r_size;
    char *chars;
    char *render;
    unsigned char *hl;
    int hl_open_comment;
} e_row;

struct e_syntax {
    char *filetype;
    char **filematch;
    char **keywords;
    char *singleline_comment_start;
    char *multiline_comment_start;
    char *multiline_comment_end;
    int flags;
};

typedef struct {
    int cx, cy;
    int render_x;
    int row_off;
    int col_off;
    int cx_off;
    int screen_rows;
    int screen_cols;
    int n_rows;
    int dirty;
    e_row *row;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    struct e_syntax *syntax;

    struct termios orig_termios;
} editorConfig;

editorConfig E;

// file types
char *C_HL_extensions[] = {".c", ".h", ".cpp", NULL};
char *C_HL_keywords[] = {
  "switch", "if", "while", "for", "break", "continue", "return", "else",
  "struct", "union", "typedef", "static", "enum", "class", "case",
  "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
  "void|", "NULL", NULL
};

struct e_syntax HLDB[] = {
    { "c", C_HL_extensions, C_HL_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

// terminal
void enable_raw_mode(void);
void disable_raw_mode(void);
void die(const char *);
int e_read_key();
int get_window_size(int *, int *);
int get_cursor_pos(int *, int *);

// row operations
void e_insert_row(int, char *, size_t);
void e_update_row(e_row *);
int e_cxrx(e_row *, int);
int e_rxcx(e_row *, int);
void e_row_insert_char(e_row *, int, int);
void e_row_delete_char(e_row *, int);
void e_free_row(e_row *);
void e_del_row(int);
void e_row_append_str(e_row *, char *, size_t);

// editor operations
void e_insert_char(int);
void e_delete_char();
void e_insert_newline();

// syntax highlighting
int is_separator(int c);
void e_update_syntax(e_row *);
int e_syntax_to_color(int);
void e_select_hl();

// file IO
void e_open(char *);
char *e_rows_to_string(int *);
void e_save();

// find
void e_find();

// append buffer
struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void ab_append(struct abuf *, const char *, int);
void ab_free(struct abuf *);

// input
void e_process_keypress();
void e_move_cursor(int);

// output
void e_clear();
void e_draw_rows(struct abuf *);
void e_scroll();
void e_draw_bar(struct abuf *);
void e_set_status_msg(const char *, ...);
void e_draw_msg(struct abuf *);
char *e_prompt(char *, void (*callback)(char *, int));

// init
void e_init();

int main(int argc, char **argv) {
    e_init();
    enable_raw_mode();
    if (argc >= 2) {
        e_open(argv[1]);
    }

    e_set_status_msg("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

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

// syntax highlighting
int is_separator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void e_update_syntax(e_row *row) {
    row->hl = realloc(row->hl, row->r_size);
    memset(row->hl, HL_NORMAL, row->r_size);

    if (E.syntax == NULL) return;

    char **keywords = E.syntax->keywords;

    char *scs = E.syntax->singleline_comment_start;
    char *mcs = E.syntax->multiline_comment_start;
    char *mce = E.syntax->multiline_comment_end;

    int scs_len = scs ? strlen(scs) : 0;
    int mcs_len = mcs ? strlen(mcs) : 0;
    int mce_len = mce ? strlen(mce) : 0;

    int prev_sep = 1;
    int in_string = 0;
    int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

    int i = 0;
    while (i < row->r_size) {
        char c = row->render[i];
        unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

        if (scs_len && !in_string && !in_comment) {
            if (!strncmp(&row->render[i], scs, scs_len)) {
                memset(&row->hl[i], HL_COMMENT, row->r_size - i);
                break;
            }
        }

        if (mcs_len && mce_len && !in_string) {
            if (in_comment) {
                row->hl[i] = HL_MLCOMMENT;
                if (!strncmp(&row->render[i], mce, mce_len)) {
                    memset(&row->hl[i], HL_MLCOMMENT, mce_len);
                    i += mce_len;
                    in_comment = 0;
                    prev_sep = 1;
                    continue;
                } else {
                    i++;
                    continue;
                }
            } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
                memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
                i += mcs_len;
                in_comment = 1;
                continue;
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
            if (in_string) {
                row->hl[i] = HL_STRING;
                if (c == '\\' && i + 1 < row->r_size) {
                    row->hl[i + 1] = HL_STRING;
                    i += 2;
                    continue;
                }
                if (c == in_string) in_string = 0;
                i++;
                prev_sep = 1;
                continue;
            } else {
                if (c == '"' || c == '\'') {
                    in_string = c;
                    row->hl[i] = HL_STRING;
                    i++;
                    continue;
                }
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
            if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
                (c == '.' && prev_hl == HL_NUMBER)) {
                row->hl[i] = HL_NUMBER;
                i++;
                prev_sep = 0;
                continue;
            }
        }

        if (prev_sep) {
            int j;
            for (j = 0; keywords[j]; j++) {
                int klen = strlen(keywords[j]);
                int kw2 = keywords[j][klen - 1] == '|';
                if (kw2) klen--;
                if (!strncmp(&row->render[i], keywords[j], klen) && is_separator(row->render[i + klen])) {
                    memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
                    i += klen;
                    break;
                }
            }
            if (keywords[j] != NULL) {
                prev_sep = 0;
                continue;
            }
        }

        prev_sep = is_separator(c);
        i++;
    }

    int changed = (row->hl_open_comment != in_comment);
    row->hl_open_comment = in_comment;
    if (changed && row->idx + 1 < E.n_rows) e_update_syntax(&E.row[row->idx + 1]);

}

int e_syntax_to_color(int hl) {
    switch (hl) {
        case HL_COMMENT:
        case HL_MLCOMMENT:  return 30;
        case HL_KEYWORD1:   return 33;
        case HL_KEYWORD2:   return 32;
        case HL_STRING:     return 35;
        case HL_NUMBER:     return 31;
        case HL_MATCH:      return 34;
        default:            return 37;
    }
}

void e_select_hl() {
    E.syntax = NULL;
    if (E.filename == NULL)
        return;
    char *ext = strrchr(E.filename, '.');
    for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
        struct e_syntax *s = &HLDB[j];
        unsigned int i = 0;
        while (s->filematch[i]) {
            int is_ext = (s->filematch[i][0] == '.');
            if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
                (!is_ext && strstr(E.filename, s->filematch[i]))) {
                E.syntax = s;
                int filerow;
                for (filerow = 0; filerow < E.n_rows; filerow++) {
                    e_update_syntax(&E.row[filerow]);
                }
                return;
            }
            i++;
        }
    }
}

// row operations
void e_insert_row(int at, char *s, size_t len) {
    if (at < 0 || at > E.n_rows) {
        return;
    }
    E.row = realloc(E.row, sizeof(e_row) * (E.n_rows + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(e_row) * (E.n_rows - at));
    for (int j = at + 1; j <= E.n_rows; j++) E.row[j].idx++;

    E.row[at].idx = at;

    E.row = realloc(E.row, sizeof(e_row) * (E.n_rows + 1));
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].r_size = 0;
    E.row[at].render = NULL;
    E.row[at].hl = NULL;
    E.row[at].hl_open_comment = 0;
    e_update_row(&E.row[at]);

    E.n_rows++;
    E.dirty++;
}

void e_update_row(e_row *row) {
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            tabs++;
        }
    }
    free(row->render);
    row->render = malloc(row->size + tabs * (PAGU_TAB_STOP - 1) + 1);
    int idx = 0;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % PAGU_TAB_STOP != 0) {
                row->render[idx++] = ' ';
            }
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->r_size = idx;

    e_update_syntax(row);
}

int e_cxrx(e_row *row, int cx) {
    int rx = 0;
    for (int i = 0; i < cx; i++) {
        if (row->chars[i] == '\t') {
            rx += (PAGU_TAB_STOP - 1) - (rx % PAGU_TAB_STOP);
        }
        rx++;
    }
    return rx;
}

int e_rxcx(e_row *row, int rx) {
    int cur_rx = 0;
    int cx;
    for (cx = 0; cx < row->size; cx++) {
        if (row->chars[cx] == '\t') {
            cur_rx += (PAGU_TAB_STOP - 1) - (cur_rx % PAGU_TAB_STOP);
        }
        cur_rx++;
        if (cur_rx > rx) {
            return cx;
        }
    }
    return cx;
}

void e_row_insert_char(e_row *row, int at, int c) {
    if (at < 0 || at > row->size) {
        at = row->size;
    }
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    e_update_row(row);
    E.dirty++;
}

void e_row_delete_char(e_row *row, int at) {
    if (at < 0 || at >= row->size) {
        return;
    }
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    e_update_row(row);
    E.dirty++;
}

void e_free_row(e_row *row) {
    free(row->render);
    free(row->chars);
    free(row->hl);
}

void e_del_row(int at) {
    if (at < 0 || at >= E.n_rows) {
        return;
    }
    e_free_row(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(e_row) * (E.n_rows - at - 1));
    for (int j = at; j < E.n_rows - 1; j++) E.row[j].idx--;
    E.n_rows--;
    E.dirty++;
}

void e_row_append_str(e_row *row, char *s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    e_update_row(row);
    E.dirty++;
}

// editor operations
void e_insert_char(int c) {
    if (E.cy == E.n_rows) {
        e_insert_row(E.n_rows, "", 0);
    }
    e_row_insert_char(&E.row[E.cy], E.cx, c);
    E.cx++;
}

void e_delete_char() {
    if (E.cy == E.n_rows || (E.cx == 0 && E.cy == 0)) {
        return;
    }

    if (E.cx > 0) {
        e_row_delete_char(&E.row[E.cy], E.cx - 1);
        E.cx--;
    } else {
        E.cx = E.row[E.cy - 1].size;
        e_row_append_str(&E.row[E.cy - 1], (&E.row[E.cy])->chars,
                         (&E.row[E.cy])->size);
        e_del_row(E.cy);
        E.cy--;
    }
}

void e_insert_newline() {
    if (E.cx == 0) {
        e_insert_row(E.cy, "", 0);
    } else {
        e_row *row = &E.row[E.cy];
        e_insert_row(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
        row = &E.row[E.cy];
        row->size = E.cx;
        row->chars[row->size] = '\0';
        e_update_row(row);
    }
    E.cy++;
    E.cx = 0;
}

// file IO
void e_open(char *filename) {
    free(E.filename);
    E.filename = strdup(filename);

    e_select_hl();

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        die("fopen");
    }
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 &&
               (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
            linelen--;
        }
        e_insert_row(E.n_rows, line, linelen);
    }
    free(line);
    fclose(fp);
    E.dirty = 0;
}

char *e_rows_to_string(int *buflen) {
    int tot_len = 0;
    int j;
    for (j = 0; j < E.n_rows; j++) {
        tot_len += E.row[j].size + 1;
    }
    *buflen = tot_len;
    char *buf = malloc(tot_len);
    char *p = buf;
    for (j = 0; j < E.n_rows; j++) {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }
    return buf;
}

void e_save() {
    if (E.filename == NULL) {
        E.filename = e_prompt("Save as: %s (ESC to abort)", NULL);
        if (E.filename == NULL) {
            e_set_status_msg("Save aborted");
            return;
        }
        e_select_hl();
    }
    int len;
    char *buf = e_rows_to_string(&len);
    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buf, len) == len) {
                close(fd);
                free(buf);
                E.dirty = 0;
                e_set_status_msg("%d bytes written to disk", len);
                return;
            }
        }
        close(fd);
    }
    free(buf);
    e_set_status_msg("Can't save! I/O error: %s", strerror(errno));
}

// find
void e_find_cb(char *query, int key) {
    static int last_match = -1;
    static int direction = 1;

    static int saved_hl_line;
    static char *saved_hl = NULL;
    if (saved_hl) {
        memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].r_size);
        free(saved_hl);
        saved_hl = NULL;
    }

    if (key == '\r' || key == '\x1b') {
        last_match = -1;
        direction = 1;
        return;
    } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction = 1;
    } else if (key == ARROW_LEFT || key == ARROW_UP) {
        direction = -1;
    } else {
        last_match = -1;
        direction = 1;
    }

    if (last_match == -1) {
        direction = 1;
    }
    int current = last_match;
    int i;
    for (i = 0; i < E.n_rows; i++) {
        current += direction;
        if (current == -1) {
            current = E.n_rows - 1;
        } else if (current == E.n_rows) {
            current = 0;
        }
        e_row *row = &E.row[current];
        char *match = strstr(row->render, query);
        if (match) {
            last_match = current;
            E.cy = current;
            E.cx = e_rxcx(row, match - row->render);
            E.row_off = E.n_rows;

            saved_hl_line = current;
            saved_hl = malloc(row->r_size);
            memcpy(saved_hl, row->hl, row->r_size);
            memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
            break;
        }
    }
}

void e_find() {
    int saved_cx = E.cx;
    int saved_cy = E.cy;
    int saved_coloff = E.col_off;
    int saved_rowoff = E.row_off;

    char *query = e_prompt("Search: %s (Use ESC/Arrows/Enter)", e_find_cb);

    if (query) {
        free(query);
    } else {
        E.cx = saved_cx;
        E.cy = saved_cy;
        E.col_off = saved_coloff;
        E.row_off = saved_rowoff;
    }
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
    static int quit_times = PAGU_QUIT_TIMES;

    int c = e_read_key();
    switch (c) {

    case '\r':
        e_insert_newline();
        break;

    case CTRL_KEY('s'):
        e_save();
        break;

    case CTRL_KEY('q'):
        if (E.dirty && quit_times > 0) {
            e_set_status_msg("WARNING! File has unsaved changes. "
                             "Press Ctrl-Q again to quit or Ctrl-S to save.");
            quit_times--;
            return;
        }
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;

    case CTRL_KEY('f'):
        e_find();
        break;

    case BACKSPACE:
    case CTRL_KEY('h'):
        e_delete_char();
        break;

    case DEL_KEY:
        e_move_cursor(ARROW_RIGHT);
        e_delete_char();
        break;

    case HOME_KEY:
        E.cx = 0;
        break;
    case END_KEY:
        if (E.cy < E.n_rows) {
            E.cx = E.row[E.cy].size;
        }
        break;

    case PAGE_UP:
    case PAGE_DOWN: {
        if (c == PAGE_UP) {
            E.cy = E.row_off;
        } else if (c == PAGE_DOWN) {
            E.cy = E.row_off + E.screen_rows - 1;
            if (E.cy > E.n_rows) {
                E.cy = E.n_rows;
            }
        }

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

    case CTRL_KEY('l'):
    case '\x1b':
        break;

    default:
        e_insert_char(c);
        break;
    }
    quit_times = PAGU_QUIT_TIMES;
}

void e_move_cursor(int key) {
    e_row *row = (E.cy >= E.n_rows) ? NULL : &E.row[E.cy];

    switch (key) {
    case ARROW_LEFT:
        if (E.cx != 0) {
            E.cx--;
        } else if (E.cy > 0) {
            E.cy--;
            E.cx = E.row[E.cy].size;
        }
        break;
    case ARROW_RIGHT:
        if (row && E.render_x < row->r_size) {
            E.cx++;
            E.render_x = e_cxrx(row, E.cx);
        } else if (row && E.render_x == row->r_size && E.cy < E.n_rows - 1) {
            E.cy++;
            E.cx = 0;
            E.render_x = 0;
        }
        break;
    case ARROW_UP:
        if (E.cy > 0) {
            E.cy--;
        }
        break;
    case ARROW_DOWN:
        if (E.cy < E.n_rows - 1) {
            E.cy++;
        }
        break;
    }

    row = (E.cy < E.n_rows) ? &E.row[E.cy] : NULL;
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen)
        E.cx = rowlen;

    if (row) {
        E.render_x = e_cxrx(row, E.cx);
    } else {
        E.render_x = 0;
    }
}

char *e_prompt(char *prompt, void (*callback)(char *, int)) {
    size_t bufsize = 128;
    char *buf = malloc(bufsize);
    size_t buflen = 0;
    buf[0] = '\0';
    while (1) {
        e_set_status_msg(prompt, buf);
        e_clear();
        int c = e_read_key();

        if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
            if (buflen != 0) {
                buf[--buflen] = '\0';
            }
        } else if (c == '\x1b') {
            e_set_status_msg("");
            if (callback) {
                callback(buf, c);
            }
            free(buf);
            return NULL;
        } else if (c == '\r') {
            if (buflen != 0) {
                e_set_status_msg("");
                if (callback) {
                    callback(buf, c);
                }
                return buf;
            }
        } else if (!iscntrl(c) && c < 128) {
            if (buflen == bufsize - 1) {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }
        if (callback) {
            callback(buf, c);
        }
    }
}

// output
void e_clear() {
    e_scroll();
    struct abuf ab = ABUF_INIT;
    ab_append(&ab, "\x1b[?25l", 6);
    ab_append(&ab, "\x1b[H", 3);
    e_draw_rows(&ab);
    e_draw_bar(&ab);
    e_draw_msg(&ab);
    char buf[32];
    E.cx_off = snprintf(NULL, 0, "%d ", E.n_rows) + 1;
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.row_off) + 1,
             (E.render_x - E.col_off) + 1 + E.cx_off);
    ab_append(&ab, buf, strlen(buf));
    ab_append(&ab, "\x1b[?25h", 6);
    write(STDOUT_FILENO, ab.b, ab.len);
    ab_free(&ab);
}

void e_draw_rows(struct abuf *ab) {
    ab_append(ab, "\x1b[K", 3); // k -> erase line
    char line_number[16];
    int line_number_width = snprintf(NULL, 0, "%d", E.n_rows) + 1;

    for (int y = 0; y < E.screen_rows; y++) {
        int filerow = y + E.row_off;
        if (filerow >= E.n_rows) {
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
            snprintf(line_number, sizeof(line_number), "%*d ",
                     line_number_width, filerow + 1);
            ab_append(ab, line_number, strlen(line_number));

            int len = E.row[filerow].r_size - E.col_off;
            if (len < 0) {
                len = 0;
            }
            if (len > E.screen_cols) {
                len = E.screen_cols;
            }
            char *c = &E.row[filerow].render[E.col_off];
            unsigned char *hl = &E.row[filerow].hl[E.col_off];
            int current_color = -1;
            int j;
            for (j = 0; j < len; j++) {
                if (iscntrl(c[j])) {
                    char sym = (c[j] <= 26) ? '@' + c[j] : '?';
                    ab_append(ab, "\x1b[7m", 4);
                    ab_append(ab, &sym, 1);
                    ab_append(ab, "\x1b[m", 3);
                    if (current_color != -1) {
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
                        ab_append(ab, buf, clen);
                    }
                } else if (hl[j] == HL_NORMAL) {
                    if (current_color != -1) {
                        ab_append(ab, "\x1b[39m", 5);
                        current_color = -1;
                    }
                    ab_append(ab, &c[j], 1);
                } else {
                    int color = e_syntax_to_color(hl[j]);
                    if (color != current_color) {
                        current_color = color;
                        char buf[16];
                        int clen =
                            snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                        ab_append(ab, buf, clen);
                    }
                    ab_append(ab, &c[j], 1);
                }
            }
            ab_append(ab, "\x1b[39m", 5);
            ab_append(ab, "\r\n", 2);
        }
        ab_append(ab, "\x1b[K", 3);
    }
}

void e_scroll() {
    E.render_x = E.cx_off;
    if (E.cy < E.n_rows) {
        E.render_x = e_cxrx(&E.row[E.cy], E.cx);
    }

    // E.render_x = E.cx;
    if (E.cy < E.row_off) {
        E.row_off = E.cy;
    }
    if (E.cy >= E.row_off + E.screen_rows - 2) {
        E.row_off = E.cy - E.screen_rows + 3;
    }
    if (E.render_x < E.col_off) {
        E.col_off = E.render_x;
    }
    if (E.render_x >= E.col_off + E.screen_cols) {
        E.col_off = E.render_x - E.screen_cols + 1;
    }
}

void e_draw_bar(struct abuf *ab) {
    ab_append(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                       E.filename ? E.filename : "[No Name]", E.n_rows,
                       E.dirty ? "(modified)" : "");
    int rlen =
        snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
                 E.syntax ? E.syntax->filetype : "no ft", E.cy + 1, E.n_rows);

    if (len > E.screen_cols) {
        len = E.screen_cols;
    }
    ab_append(ab, status, len);
    while (len < E.screen_cols) {
        if (E.screen_cols - len == rlen) {
            ab_append(ab, rstatus, rlen);
            break;
        } else {
            ab_append(ab, " ", 1);
            len++;
        }
    }
    ab_append(ab, "\x1b[m", 3);
    ab_append(ab, "\r\n", 2);
}

void e_set_status_msg(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

void e_draw_msg(struct abuf *ab) {
    ab_append(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screen_cols) {
        msglen = E.screen_cols;
    }
    if (msglen && time(NULL) - E.statusmsg_time < 5) {
        ab_append(ab, E.statusmsg, msglen);
    }
}

// init
void e_init() {
    E.cx = 0;
    E.cy = 0;
    E.cx_off = 0;
    E.render_x = 0;
    E.n_rows = 0;
    E.dirty = 0;
    E.row = NULL;
    E.row_off = 0;
    E.col_off = 0;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    E.syntax = NULL;

    if (get_window_size(&E.screen_rows, &E.screen_cols) == -1) {
        die("get_window_size");
    }
    E.screen_rows -= 2;
}
