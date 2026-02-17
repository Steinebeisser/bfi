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
        .x = w.ws_row,
        .y = w.ws_col
    };
}
#endif

#define CSI "\033["
#define CLEAR CSI"2J"
#define HOME CSI"H"
#define HIDE_CUROSR CSI"?25l"
#define SHOW_CURSOR CSI"?25h"
#define COLOR_BG_BLUE CSI"44;97m"
#define COLOR_RESET CSI"0m"


#define TOGGLE_AUTO_STEP 'a'
#define STEP 's'
#define STEP_5 'n'
#define INCREASE_AUTO_STEP 
#define DECREASE_AUTO_STEP 

void show_cursor() {
    printf(SHOW_CURSOR);
}

void draw_bf(const char *buf, size_t buf_len, u8 *tape, size_t ip, size_t dp) {
    printf(CLEAR HOME HIDE_CUROSR);
    Vec2 term_width = get_term_width();

    int tape_width = term_width.x - 20;
    int half_window = tape_width / 2;

    size_t start;
    if (ip < half_window) {
        start = 0;
    } else if (ip + half_window >= buf_len) {
        start = buf_len > tape_width ? buf_len - tape_width : 0;
    } else {
        start = ip - half_window;
    }

    for (size_t i = start; i < buf_len && i < start + tape_width; ++i) {
        if (i == ip) {
            printf(COLOR_BG_BLUE "%c" COLOR_RESET, buf[i]);
        } else {
            putchar(buf[i]);
        }
    }
    printf("\n");

    for (size_t i = start; i < buf_len && i < start + tape_width; ++i) {
        putchar(i == ip ? '^' : ' ');
    }
    printf("\n\n");

    printf("\n\n");


    for (size_t i = start; i < tape_width; ++i) {
    }

    // PROGRAM
    // . . .. .. >+++ [ ] etc hightlight current location
    //
    //
    // TAPE [][][][][][][][][][] 
    // draw cells with value mark current cell
    //
    //
    // INSTRUCTIONS
    // a = auto step
    // s = step
    // n = step 5
    // -> increase auto step speed
    // <- decrease auto step speed
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
                if (ip == 0 && nested != 0 && instruction != '[') {
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
