#include <iso646.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

typedef uint8_t u8;

bool enable_raw_mode();
void disable_raw_mode();

typedef struct {
    int x, y;
} Vec2;

Vec2 get_term_width();

#ifdef __WIN32
#include <windows.h>

static HANDLE hOut = INVALID_HANDLE_VALUE;
static DWORD original_output_mode = 0;

static UINT g_oldOutputCP = 0;
static UINT g_oldInputCP = 0;

bool raw_mode_enabled = false;
static DWORD original_input_mode = 0;
static HANDLE hIn = INVALID_HANDLE_VALUE;

bool enable_raw_mode() {
    if (raw_mode_enabled) {
        return true;
    }

    SetConsoleCtrlHandler(NULL, TRUE);

    hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (!GetConsoleMode(hIn, &original_input_mode)) {
        return false;
    }
    DWORD mode = original_input_mode;

    mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    if (!SetConsoleMode(hIn, mode)) {
        return false;
    }

    hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) {
        return false;
    }

    if (!GetConsoleMode(hOut, &original_output_mode)) {
        return false;
    }
    DWORD dwMode = original_output_mode;

    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN | ENABLE_PROCESSED_OUTPUT;
    if (!SetConsoleMode(hOut, dwMode)) {
        return false;
    }

    g_oldOutputCP = GetConsoleOutputCP();
    g_oldInputCP = GetConsoleCP();

    if (!SetConsoleOutputCP(65001)) return false;
    if (!SetConsoleCP(65001)) return false;

    atexit(disable_raw_mode);
    raw_mode_enabled = true;
}

void disable_raw_mode() {
    if (raw_mode_enabled && original_input_mode != 0 && hIn != INVALID_HANDLE_VALUE) {
        FlushConsoleInputBuffer(hIn);
        SetConsoleMode(hIn, original_input_mode);
        raw_mode_enabled = false;
    }


    if (original_output_mode != 0 && hOut != INVALID_HANDLE_VALUE) {
        SetConsoleMode(hOut, original_output_mode);
    }

    if (g_oldOutputCP != 0) {
        SetConsoleOutputCP(g_oldOutputCP);
    }
    if (g_oldInputCP != 0) {
        SetConsoleCP(g_oldInputCP);
    }
}

Vec2 get_term_width() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int columns, rows;

    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    columns = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

    return (Vec2) {
        .x = columns,
        .y = rows
    };
}

#endif


#if defined (__unix__)
    #include <termios.h>
    #include <unistd.h>
    #include <sys/ioctl.h>
static struct termios orig_termios;


bool enable_raw_mode() {

    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        return false;
    }
    atexit(disable_raw_mode);
    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        return false;
    }
    return true;
}

void disable_raw_mode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) {
        fprintf(stderr, "Failed to disable raw mode\n");
    }
}

Vec2 get_term_width() {
    struct winsize w;
    ioctl(0, TIOCGWINSZ, &w);

    return (Vec2){
        .x = w.ws_col,
        .y = w.ws_row
    };
}
#endif

#define CSI "\033["
#define CLEAR CSI"2J"
#define HOME CSI"H"
#define HIDE_CUROSR CSI"?25l"
#define SHOW_CURSOR CSI"?25h"
#define CURSOR_UP(n)   CSI #n "A"
#define CURSOR_DOWN(n) CSI #n "B"
#define CURSOR_RIGHT(n) CSI #n "C"
#define CURSOR_LEFT(n) CSI #n "D"

#define COLOR_BG_BLUE CSI"44m"

#define COLOR_FG_GREEN CSI"32m"
#define COLOR_FG_GRAY CSI"90m"

#define COLOR_RESET CSI"0m"


void show_cursor() {
    printf(SHOW_CURSOR);
}

void handle_sigint(int sig) {
    disable_raw_mode();
    show_cursor();
    printf("\n");
    exit(0);
}

#define MIN_WIDTH 48
#define MIN_HEIGHT 12

#define CELL_WIDTH 6 // `[xxx] `
#define PADDING_SIDE 16

#define AUTO_PLAY_MIN_MS 50
#define AUTO_PLAY_MAX_MS 2000
#define AUTO_PLAY_DEFAULT_MS 300
#define AUTO_PLAY_STEP_MS 50

static char drawn_output[250] = {0};
static int drawn_output_pos = 0;

void draw_bf(const char *buf, size_t buf_len, u8 *tape, size_t ip, size_t dp) {
    printf(CLEAR HOME HIDE_CUROSR);
    Vec2 term = get_term_width();

    if (term.x < MIN_WIDTH || term.y < MIN_HEIGHT) {
        fprintf(stderr, "Terminal too small, must be at least: %d:%d\n", MIN_WIDTH, MIN_HEIGHT);
        exit(1);
    }

    int avail = term.x - 2 * PADDING_SIDE;
    if (buf_len < avail) avail = buf_len;
    if (avail < MIN_WIDTH) avail = MIN_WIDTH;
    int possible_cells = avail / CELL_WIDTH;
    int missing = avail - possible_cells * CELL_WIDTH;


    int true_avail = avail - missing - 1; //- 1 for extra space last cell has padded

    int center = true_avail / 2 + PADDING_SIDE;

    int half = true_avail / 2;


    int first_program_instruction;
    if (ip < true_avail / 2) {
        first_program_instruction = 0; 
    }
    else if ((int)ip + half >= (int)buf_len) {
        first_program_instruction = (int)buf_len - true_avail;
        if (first_program_instruction < 0) first_program_instruction = 0;
    } else {
        first_program_instruction = ip - true_avail / 2;
    }

    int last_program_instruction = first_program_instruction + true_avail;
    int trunc_left_count  = first_program_instruction;
    int trunc_right_count = (int)buf_len - last_program_instruction;


    if (trunc_left_count > 0) {
        char left_label[PADDING_SIDE];
        int visible_len = snprintf(NULL, 0, "+%d ", trunc_left_count);
        snprintf(left_label, sizeof(left_label),COLOR_FG_GRAY "+%d " COLOR_RESET, trunc_left_count);
        printf("%*s%s", PADDING_SIDE - visible_len, "", left_label);
    } else {
        printf("%*s", PADDING_SIDE, "");
    }

    for (int i = first_program_instruction; i < first_program_instruction + true_avail ; ++i) {
        if (i == ip) {
           printf(COLOR_BG_BLUE "%c" COLOR_RESET CURSOR_LEFT(1) CURSOR_DOWN(1) "^" CURSOR_UP(1) , buf[i]);
        } else {
            putchar(buf[i]);
        }
    }

    if (trunc_right_count > 0) {
        printf(COLOR_FG_GRAY " +%d" COLOR_RESET, trunc_right_count);
    }

    printf("\n\n\n\n");


    int first_cell;
    if (dp < possible_cells / 2) {
        first_cell = 0;
    } else {
        first_cell = dp - possible_cells / 2;
    }

    printf("%*s", PADDING_SIDE, "");
    for (int i = first_cell; i < first_cell + possible_cells; ++i) {
        if (i == dp) {
            printf(COLOR_BG_BLUE "[%03u]" COLOR_RESET " ", tape[i]);
        } else {
            printf("[%03u] ", tape[i]);
        }

        printf(CURSOR_UP(1) CURSOR_LEFT(5) "%-4d" CURSOR_RIGHT() CURSOR_DOWN(1), i);

        if (32 <= tape[i] && tape[i] <= 126) {
            printf(COLOR_FG_GREEN CURSOR_DOWN(1) CURSOR_LEFT(4) "%c" CURSOR_RIGHT(3) CURSOR_UP(1) COLOR_RESET, tape[i]);
        } else {
            printf(COLOR_FG_GRAY CURSOR_DOWN(1) CURSOR_LEFT(4) "." CURSOR_RIGHT(3) CURSOR_UP(1) COLOR_RESET);
        }
    }

    printf("\n\n\n\n");


    if (buf[ip] == '.') {
        drawn_output[drawn_output_pos++] = tape[dp];
    }

    printf("%*s", PADDING_SIDE, "");
    printf("┌");
    for (int i = 0; i < true_avail - 2; ++i)
        printf("─");

    printf("┐\n");
    printf("%*s│ ", PADDING_SIDE, "");
    int box_inner_width = true_avail - 2;

    int col = 0;
    for (int i = 0; i < drawn_output_pos; ++i) {
        char c = drawn_output[i];
        if (c == '\n' || col >= box_inner_width - 1) {
            printf("%*s│\n", box_inner_width - 1 - col, "");
            printf("%*s│ ", PADDING_SIDE, "");
            col = 0;
            if (c == '\n') continue;
        }
        if (32 <= c && c <= 126) {
            putchar(c);
        } else {
            putchar('.');
        }
        col++;
    }
    printf("%*s│\n", box_inner_width - 1 - col, "");
    printf("%*s└", PADDING_SIDE, "");

    for (int i = 0; i < true_avail - 2; ++i) printf("─");
        printf("┘\n");

    // PROGRAM
    // IP
    // <- ... [30] ++++>.
    //   ^
    //
    // TAPE
    //   12    13
    // [000] [000]
    //   ^
    //   .     .
    //
    //
    // OUTPUT
    // |--------------
    // |
    // |
    // |
    // |
    // |--------------
    // COMMANDS
    //
    // a = toggle auto play
    // f = faster
    // s = slower
    // any other key = manual step
}

int execute_step(const char *buf, size_t buf_len, u8 *tape, size_t *ip, size_t *dp) {

    if (*ip >= buf_len) return -1;

    char instruction = buf[*ip];

    switch (instruction) {
        case '>':
            (*dp)++;
            break;

        case '<':
            if (*dp > 0) (*dp)-=1;
            break;

        case '+':
            tape[*dp] += 1;
            break;

        case '-':
            tape[*dp] -= 1;
            break;

        case '.':
            putc(tape[*dp], stdout);
            break;

        case ',':
            tape[*dp] = getc(stdin);
            break;

        case '[':
            if (tape[*dp] != 0) break;
            size_t nested = 0;
            do {
                *ip += 1;
                if (*ip >= buf_len) {
                    fprintf(stderr, "%s\n", "Invalid Program\nUnmachteed '['");
                    return 1;
                }

                if (buf[*ip] == '[') nested += 1;
                else if (buf[*ip] == ']') {
                    if (nested == 0) break;
                    nested -= 1;
                }
            } while(1);
            break;

        case ']': {
            if (tape[*dp] == 0) break;

            size_t nested = 0;
            do {
                *ip -= 1;
                if (*ip == 0 && nested != 0 && instruction != '[') {
                    fprintf(stderr, "%s\n", "Invalid Program\nUnmachteed ']'");
                    return 1;
                }

                if (buf[*ip] == ']') nested += 1;
                else if (buf[*ip] == '[') {
                    if (nested == 0) break;
                    nested -= 1;
                }
            } while (1);
        }
    }

    (*ip)++;
    return 0;
}


int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: ./bfi <file> [flags]\n");
        return 1;
    }

    signal(SIGINT, handle_sigint);
    bool visualize = false;
    bool help = false;

    if (argc >= 3) {
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--visualize") == 0) visualize = true;
            if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) help = true;
        }
    }

    if (help) {
        printf("Usage: ./bfi <file> [flags]\n\n");
        printf("Flags:\n    -h/--help: prints this message\n    -v/--visualize: goes into visualize mode\n");
        return 0;
    }

    FILE *f = fopen(argv[1], "r");
    if (!f) return 1;

    fseek(f, 0, SEEK_END);
    size_t source_len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc(source_len);
    if (!buf) return 1;

    size_t wrote = fread(buf, sizeof(char), source_len, f);
    if (source_len != wrote) return 1;

    u8 tape[30000] = {0};

    size_t dp = 0;
    size_t ip = 0;

    if (visualize) {
        enable_raw_mode();

        printf(CLEAR HOME HIDE_CUROSR);
        atexit(show_cursor);

        bool auto_step = false;
        while (ip < source_len) {
            draw_bf(buf, source_len, tape, ip, dp);
            execute_step(buf, source_len, tape, &ip, &dp);
            getc(stdin);
        }
    } else {
        while (ip < source_len) {
            execute_step(buf, source_len, tape, &ip, &dp);
        }
    }

    return 0;
}
