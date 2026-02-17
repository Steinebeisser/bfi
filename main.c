#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef uint8_t u8;

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: ./bfi <file>\n");
        return 1;
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

    for (;ip < source_len; ++ip) {
        switch (buf[ip]) {
            case '>': dp++; break;
            case '<': dp > 0 ? dp-- : dp; break;
            case '+': tape[dp] += 1; break;
            case '-': tape[dp] -= 1; break;
            case '.': putc(tape[dp], stdout); break;
            case ',': tape[dp] = getc(stdin); break;
            case '[':
                if (tape[dp] != 0) break;
                size_t nested = 0;
                do {
                    ip += 1;
                    if (ip >= source_len) {
                        fprintf(stderr, "%s\n", "Invalid Program\nUnmachteed '['");
                        return 1;
                    }

                    if (buf[ip] == '[') nested += 1;
                    else if (buf[ip] == ']') {
                        if (nested == 0) break;
                        nested -= 1;
                    }
                } while(1);
            case ']': {
                if (tape[dp] == 0) break;

                size_t nested = 0;
                do {
                    ip -= 1;
                    if (ip == 0 && nested != 0 && buf[ip] != '[') {
                        fprintf(stderr, "%s\n", "Invalid Program\nUnmachteed ']'");
                        return 1;
                    }

                    if (buf[ip] == ']') nested += 1;
                    else if (buf[ip] == '[') {
                        if (nested == 0) break;
                        nested -= 1;
                    }
                } while (1);
            }
        }
    }

    return 0;
}
