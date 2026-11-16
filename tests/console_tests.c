#ifdef NDEBUG
#undef NDEBUG
#endif
#include "console.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *in;
    char out[65536];
    size_t length;
    uint8_t file[1024];
    size_t file_length;
    unsigned saves, loads, dirs, polls;
    int interrupt_after;
} Fixture;
static Fixture f;
static Console c;
static int receive(void *context, uint32_t timeout) {
    Fixture *m = context;
    (void)timeout;
    return *m->in ? (unsigned char)*m->in++ : -1;
}
static void send(void *context, const void *data, size_t n) {
    Fixture *m = context;
    assert(n < sizeof(m->out) - m->length);
    memcpy(m->out + m->length, data, n); m->length += n; m->out[m->length] = 0;
}
static int command(void *context, const char *verb, const char *argument) {
    Fixture *m = context;
    if (strcmp(verb, "DIR")) return 0;
    assert(!strcmp(argument, "/test dir"));
    ++m->dirs; return 1;
}
static int load(void *context, const char *path, uint8_t *data, size_t capacity, size_t *length) {
    Fixture *m = context;
    if (strcmp(path, "hello world.bas") || !m->saves) return -1;
    assert(m->file_length <= capacity);
    memcpy(data, m->file, m->file_length); *length = m->file_length; ++m->loads;
    return 0;
}
static int save(void *context, const char *path, const uint8_t *data, size_t n) {
    Fixture *m = context;
    assert(!strcmp(path, "hello world.bas"));
    assert(n <= sizeof(m->file));
    memcpy(m->file, data, n); m->file_length = n; ++m->saves; return 0;
}
static int interrupted(void *context) {
    Fixture *m = context;
    return m->interrupt_after && ++m->polls == (unsigned)m->interrupt_after;
}
static void exercise(const char *input, int break_after) {
    ConsoleIO io = {
        .context = &f, .receive = receive, .send = send, .load = load,
        .save = save, .command = command, .break_requested = interrupted
    };
    memset(&f, 0, sizeof(f)); f.in = input; f.interrupt_after = break_after;
    console_run(&c, &io);
}
static void check_help_page(const char *input, const char *title, const char *expected) {
    exercise(input, 0);
    const char *row = strstr(f.out, title);
    assert(row && strstr(row, expected));
    unsigned rows = 0;
    while (strncmp(row, "BASIC> ", 7)) {
        const char *end = strstr(row, "\r\n");
        assert(end && end - row < 80);
        ++rows;
        row = end + 2;
    }
    assert(rows > 0 && rows <= 22);
}
int main(void) {
    check_help_page("HELP\r", "STRUCTURED BASIC - QUICK HELP", "HELP BASIC    HELP EDIT    HELP SD");
    check_help_page("hElP bAsIc\r", "HELP BASIC - WRITE", "APPEND PRINT \"Hello, Nucleo!\"");
    check_help_page("HELP EDIT\r", "HELP EDIT - FULL-SCREEN", "Ctrl+Q / Esc");
    check_help_page("HELP SD\r", "HELP SD - FILES", "UMOUNT           Unmount");
    exercise("HELP unknown\r", 0);
    assert(strstr(f.out, "Unknown help topic."));
    exercise("BASIC PRINT 2+34\x1b[D\x1b[3~\r\n"
             "BASIC PRINT 42X\x7f\r"
             "BASIC PRINT 17x\x1b[H\x1b[F\x7f\r"
             "DIR \"/test dir\"\r", 0);
    assert(strstr(f.out, "\r\n5\r\nBASIC> "));
    assert(strstr(f.out, "\r\n42\r\nBASIC> "));
    assert(strstr(f.out, "\r\n17\r\nBASIC> "));
    assert(f.dirs == 1);
    exercise("NEW \"hello world.bas\"\rEDIT\rPRINT \"one\"\rPRINT \"two\"\x1b[A\x1b[H"
             "'\x13\x11RUN\rNEW\rLOAD \"hello world.bas\"\rRUN\r"
             "RUN \"hello world.bas\"\r", 0);
    assert(f.saves == 1 && f.loads == 2);
    assert(strstr(f.out, "Structured BASIC editor"));
    assert(strstr(f.out, "\r\ntwo\r\nBASIC> "));
    assert(f.file_length == strlen("'PRINT \"one\"\nPRINT \"two\""));
    assert(!memcmp(f.file, "'PRINT \"one\"\nPRINT \"two\"", f.file_length));
    exercise("APPEND INPUT N\rAPPEND PRINT N*2\rRUN\r20\x7f" "1\rNEW\rn\rRUN\r3\r", 0);
    assert(strstr(f.out, "\r\n42\r\nBASIC> ") && strstr(f.out, "\r\n6\r\nBASIC> "));
    exercise("APPEND WHILE 1\rAPPEND WEND\rRUN\rBASIC PRINT 9\r", 25);
    assert(strstr(f.out, "Execution interrupted") && strstr(f.out, "\r\n9\r\nBASIC> "));
    exercise("BASIC PRINT 7\r\x1b[A\r", 0);
    const char *first = strstr(f.out, "\r\n7\r\nBASIC> ");
    assert(first && strstr(first + 1, "\r\n7\r\nBASIC> "));
    exercise("APPEND DIM A(3000)\rAPPEND A(2999)=73\rAPPEND PRINT A(2999)\r"
             "RUN\rRUN\rBASIC DIM B(4500)\rBASIC PRINT 19\r", 0);
    const char *large_array = strstr(f.out, "\r\n73\r\nBASIC> ");
    assert(large_array && strstr(large_array + 1, "\r\n73\r\nBASIC> "));
    assert(strstr(f.out, "BASIC memory pool exhausted"));
    assert(strstr(f.out, "\r\n19\r\nBASIC> "));
    puts("Console PASS: 80x24 help, VT100 editing, history, save/reload, INPUT, BREAK and 16 KiB pool");
    return 0;
}
