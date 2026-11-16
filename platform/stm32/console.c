#include "console.h"
#include <stdio.h>
#include <string.h>

enum { KEY_LEFT = 256, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_HOME, KEY_END, KEY_DEL, KEY_PGUP, KEY_PGDN };
enum { EDIT_ROWS = 21, EDIT_COLUMNS = 78 };

static void bytes(Console *c, const void *data, size_t n) { c->io.send(c->io.context, data, n); }
static void say(Console *c, const char *s) { bytes(c, s, strlen(s)); }
static void place(Console *c, size_t row, size_t column) {
    char seq[32];
    int n = snprintf(seq, sizeof(seq), "\x1b[%u;%uH", (unsigned)row, (unsigned)column);
    if (n > 0 && (size_t)n < sizeof(seq)) bytes(c, seq, (size_t)n);
}
static int key(Console *c) {
    int ch, op, parameter;
    for (;;) {
        ch = c->io.receive(c->io.context, UINT32_MAX);
        if (ch < 0) return ch;
        if (ch == '\n' && c->skip_lf) { c->skip_lf = 0; continue; }
        c->skip_lf = (uint8_t)(ch == '\r');
        if (ch == '\r') return '\n';
        if (ch != 27) return ch;
        op = c->io.receive(c->io.context, 45);
        if (op != '[' && op != 'O') return 27;
        parameter = -1;
        for (unsigned i = 0; i < 12; ++i) {
            op = c->io.receive(c->io.context, 45);
            if (op < 0) return 0;
            if (i == 0 && op >= '0' && op <= '9') parameter = op - '0';
            if (op >= '@' && op <= '~') break;
        }
        switch (op) {
        case 'A': return KEY_UP; case 'B': return KEY_DOWN;
        case 'C': return KEY_RIGHT; case 'D': return KEY_LEFT;
        case 'H': return KEY_HOME; case 'F': return KEY_END;
        case '~':
            if (parameter == 3) return KEY_DEL;
            if (parameter == 1 || parameter == 7) return KEY_HOME;
            if (parameter == 4 || parameter == 8) return KEY_END;
            if (parameter == 5) return KEY_PGUP;
            if (parameter == 6) return KEY_PGDN;
            break;
        }
    }
}
static int readline(Console *c, const char *prompt, uint8_t *line, size_t capacity, uint16_t *length,
                    int history) {
    size_t n = 0, cursor = 0, left = 0;
    size_t width = EDIT_COLUMNS - strlen(prompt);
    say(c, prompt);
    for (;;) {
        int ch = key(c);
        if (ch < 0) return 1;
        if (ch == 3) { say(c, "^C\r\n"); return -2; }
        if (ch == '\n') {
            say(c, "\r\n");
            *length = (uint16_t)n;
            if (history && n) { memcpy(c->history, line, n); c->history[n] = '\0'; }
            return 0;
        }
        if (ch == KEY_LEFT && cursor) --cursor;
        else if (ch == KEY_RIGHT && cursor < n) ++cursor;
        else if (ch == KEY_HOME || ch == 1) cursor = 0;
        else if (ch == KEY_END || ch == 5) cursor = n;
        else if (ch == 21 || (ch == KEY_DOWN && history)) n = cursor = 0;
        else if (ch == KEY_UP && history) {
            n = strlen(c->history);
            if (n > capacity) n = capacity;
            memcpy(line, c->history, n); cursor = n;
        } else if ((ch == 8 || ch == 127) && cursor) {
            memmove(line + cursor - 1, line + cursor, n - cursor); --n; --cursor;
        } else if (ch == KEY_DEL && cursor < n) {
            memmove(line + cursor, line + cursor + 1, n - cursor - 1); --n;
        } else if (ch >= 32 && ch <= 255) {
            if (n == capacity) { say(c, "\a"); continue; }
            memmove(line + cursor + 1, line + cursor, n - cursor);
            line[cursor++] = (uint8_t)ch; ++n;
        } else continue;
        if (cursor < left) left = cursor;
        if (cursor >= left + width) left = cursor - width + 1;
        size_t shown = n - left < width ? n - left : width;
        say(c, "\r\x1b[2K"); say(c, prompt); bytes(c, line + left, shown);
        if (shown > cursor - left) {
            char seq[24];
            int count = snprintf(seq, sizeof(seq), "\x1b[%uD", (unsigned)(shown - (cursor - left)));
            if (count > 0) bytes(c, seq, (size_t)count);
        }
    }
}
static int vm_read(void *p, uint32_t offset) { return ((const uint8_t *)p)[offset]; }
static int vm_write(void *p, const uint8_t *data, uint16_t n) {
    Console *c = p;
    uint16_t start = 0;
    for (uint16_t i = 0; i < n; ++i) {
        if (data[i] == '\n') {
            bytes(c, data + start, (size_t)(i - start));
            say(c, "\r\n"); start = (uint16_t)(i + 1);
        }
    }
    bytes(c, data + start, (size_t)(n - start)); return 0;
}
static int vm_input(void *p, uint8_t *data, uint16_t cap, uint16_t *len) {
    return readline(p, "? ", data, cap, len, 0);
}
static int vm_poll(void *p) {
    Console *c = p;
    return c->io.break_requested && c->io.break_requested(c->io.context);
}
static int vm_device(void *p, uint8_t op, int32_t a, int32_t b, int32_t *result) {
    Console *c = p;
    return c->io.device ? c->io.device(c->io.context, op, a, b, result) : -1;
}
static void run(Console *c, uint8_t *program, size_t n) {
    SBSource src = {vm_read, program, (uint32_t)n};
    SBPlatform platform = {0};
    platform.user = c;
    platform.write = vm_write;
    platform.input = vm_input;
    platform.poll = vm_poll;
    platform.device = vm_device;
    SBError error = sb_compile(&c->basic, src);
    if (error == SB_OK) error = sb_execute(&c->basic, &platform, UINT32_MAX, 1);
    if (error != SB_OK) {
        char diagnostic[160];
        int count = snprintf(diagnostic, sizeof(diagnostic), "E%u line %u: %s\r\n",
                             (unsigned)error, c->basic.error_line, sb_error_message(error));
        if (count > 0) say(c, diagnostic);
    }
}
static int resolve(Console *c, const char *p, char *absolute) {
    if (*p && c->io.resolve) return !c->io.resolve(c->io.context, p, absolute, CONSOLE_PATH_CAPACITY);
    size_t n = strlen(p);
    if (n >= CONSOLE_PATH_CAPACITY) return 0;
    memcpy(absolute, p, n + 1); return 1;
}
static int save(Console *c, const char *path) {
    char absolute[CONSOLE_PATH_CAPACITY];
    if (!*path) { say(c, "Use SAVE filename or NEW filename first.\r\n"); return 0; }
    if (!resolve(c, path, absolute) || !c->io.save ||
        c->io.save(c->io.context, absolute, c->source, c->length)) return 0;
    strcpy(c->path, absolute); c->dirty = 0; say(c, "Saved.\r\n"); return 1;
}
static int load(Console *c, const char *path, size_t *len) {
    if (!*path) { say(c, "A filename is required.\r\n"); return 0; }
    return c->io.load && !c->io.load(c->io.context, path, c->incoming, sizeof(c->incoming), len);
}
static void load_editor(Console *c, size_t length, const char *path) {
    size_t out = 0;
    for (size_t i = 0; i < length; ++i) {
        uint8_t ch = c->incoming[i];
        if (ch == '\r') {
            if (i + 1 < length && c->incoming[i + 1] == '\n') continue;
            ch = '\n';
        }
        c->source[out++] = ch;
    }
    c->length = out; c->dirty = 0; strcpy(c->path, path);
}
static size_t line_start(Console *c, size_t p) {
    while (p && c->source[p - 1] != '\n') --p;
    return p;
}
static size_t line_end(Console *c, size_t p) {
    while (p < c->length && c->source[p] != '\n') ++p;
    return p;
}
static void repaint(Console *c, size_t top, size_t left) {
    size_t p = top;
    place(c, 1, 1);
    say(c, "\x1b[2KStructured BASIC editor  ");
    /* Filenames are user input. Do not render control bytes as escape sequences. */
    for (const char *s = c->path; *s && s < c->path + 48; ++s) {
        unsigned char ch = (unsigned char)*s;
        if (ch >= 32 && ch < 127) bytes(c, s, 1);
    }
    for (size_t row = 0; row < EDIT_ROWS; ++row) {
        say(c, "\r\n\x1b[2K");
        if (p > c->length) continue;
        size_t end = line_end(c, p);
        size_t start = end - p >= left ? p + left : end;
        for (size_t i = start; i < end && i - start < EDIT_COLUMNS; ++i) {
            uint8_t ch = c->source[i];
            if (ch == '\t') ch = ' ';
            else if (ch < 32 || ch > 126) ch = '.';
            bytes(c, &ch, 1);
        }
        p = end + 1;
    }
    say(c, "\r\n\x1b[2KCtrl+S Save   Ctrl+Q / Esc Return   Arrows / Home / End / PgUp / PgDn");
}
static void editor(Console *c) {
    size_t cursor = 0, top = 0, left = 0, goal = 0;
    int changed = 1, vertical = 0;
    say(c, "\x1b[2J");
    for (;;) {
        size_t start = line_start(c, cursor), row = 0, p;
        if (start < top) { top = start; changed = 1; }
        for (p = top; p < start; p = line_end(c, p) + 1) ++row;
        while (row >= EDIT_ROWS) { top = line_end(c, top) + 1; --row; changed = 1; }
        size_t col = cursor - start;
        if (col < left) { left = col; changed = 1; }
        if (col >= left + EDIT_COLUMNS) { left = col - EDIT_COLUMNS + 1; changed = 1; }
        if (changed) repaint(c, top, left);
        place(c, EDIT_ROWS + 3, 1);
        say(c, c->dirty ? "\x1b[2KModified (in RAM)" : "\x1b[2K");
        place(c, row + 2, col - left + 1);
        changed = 0;
        int ch = key(c);
        if (ch < 0 || ch == 17 || ch == 27 || ch == 3) break;
        if (ch == 19) {
            place(c, EDIT_ROWS + 3, 1); say(c, "\x1b[2K"); save(c, c->path); changed = 1; continue;
        }
        if (ch == KEY_UP || ch == KEY_DOWN || ch == KEY_PGUP || ch == KEY_PGDN) {
            unsigned times = (ch == KEY_PGUP || ch == KEY_PGDN) ? EDIT_ROWS : 1;
            if (!vertical) goal = col;
            vertical = 1;
            while (times) {
                --times;
                if (ch == KEY_UP || ch == KEY_PGUP) {
                    if (!start) break;
                    start = line_start(c, start - 1);
                } else {
                    size_t end = line_end(c, start);
                    if (end == c->length) break;
                    start = end + 1;
                }
                size_t width = line_end(c, start) - start;
                cursor = start + (goal < width ? goal : width);
            }
            continue;
        }
        vertical = 0;
        if (ch == KEY_LEFT && cursor) --cursor;
        else if (ch == KEY_RIGHT && cursor < c->length) ++cursor;
        else if (ch == KEY_HOME) cursor = start;
        else if (ch == KEY_END) cursor = line_end(c, cursor);
        else if (((ch == 8 || ch == 127) && cursor) || (ch == KEY_DEL && cursor < c->length)) {
            if (ch != KEY_DEL) --cursor;
            memmove(c->source + cursor, c->source + cursor + 1, c->length - cursor - 1);
            --c->length; changed = c->dirty = 1;
        } else if ((ch >= 32 && ch <= 255) || ch == '\n' || ch == '\t') {
            if (c->length == sizeof(c->source)) { say(c, "\a"); continue; }
            memmove(c->source + cursor + 1, c->source + cursor, c->length - cursor);
            c->source[cursor++] = (uint8_t)ch; ++c->length; changed = c->dirty = 1;
        }
    }
    say(c, "\x1b[2J\x1b[H");
    if (c->dirty) say(c, "Changes held in RAM. Use SAVE to write to SD.\r\n");
}
static char *strip(char *p) {
    while (*p == ' ' || *p == '\t') ++p;
    size_t n = strlen(p);
    while (n && (p[n - 1] == ' ' || p[n - 1] == '\t')) p[--n] = 0;
    if (n >= 2 && p[0] == '"' && p[n - 1] == '"') { p[n - 1] = 0; ++p; }
    return p;
}
static int replacing(Console *c) {
    if (!c->dirty) return 1;
    uint8_t reply[8]; uint16_t n;
    return !readline(c, "Discard unsaved buffer? [y/N] ", reply, sizeof(reply), &n, 0) &&
           n == 1 && (reply[0] == 'y' || reply[0] == 'Y');
}
static int help_topic(const char *argument, const char *topic) {
    while (*argument && *topic) {
        char ch = *argument++;
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        if (ch != *topic++) return 0;
    }
    return !*argument && !*topic;
}
static void show_help(Console *c, const char *topic) {
    if (!*topic) {
        say(c, "STRUCTURED BASIC - QUICK HELP\r\n"
               "\r\n"
               "PROGRAM\r\n"
               "  NEW [file]       Start a new program in memory.\r\n"
               "  EDIT [file]      Open the full-screen editor.\r\n"
               "  LIST             Show the current program.\r\n"
               "  RUN [file]       Run the current program or a file from SD.\r\n"
               "  BASIC PRINT 2+2  Try one BASIC statement.\r\n"
               "\r\n"
               "SD CARD\r\n"
               "  DIR [path]       List files and directories.\r\n"
               "  LOAD file        Load an existing program.\r\n"
               "  SAVE [file]      Save the program to SD.\r\n"
               "\r\n"
               "KEYS\r\n"
               "  In the editor    Ctrl+S  Save     Ctrl+Q / Esc  Return\r\n"
               "  At any time      Ctrl+C  Cancel input or stop a program\r\n"
               "\r\n"
               "More help: HELP BASIC    HELP EDIT    HELP SD\r\n");
    } else if (help_topic(topic, "BASIC")) {
        say(c, "HELP BASIC - WRITE AND RUN A PROGRAM\r\n"
               "\r\n"
               "  BASIC statement  Run one statement without changing the program.\r\n"
               "  NEW [file]       Clear the buffer and optionally name the program.\r\n"
               "  APPEND text      Add one source line to the buffer.\r\n"
               "  LIST             Show the source in memory.\r\n"
               "  RUN [file]       Run the buffer or a file. The buffer stays intact.\r\n"
               "\r\n"
               "Try these commands in order:\r\n"
               "  NEW HELLO.BAS\r\n"
               "  APPEND PRINT \"Hello, Nucleo!\"\r\n"
               "  RUN\r\n"
               "  SAVE\r\n");
    } else if (help_topic(topic, "EDIT")) {
        say(c, "HELP EDIT - FULL-SCREEN EDITOR\r\n"
               "\r\n"
               "  EDIT              Edit the program in memory.\r\n"
               "  EDIT file         Load and edit an existing file.\r\n"
               "  NEW file          Give a new program a filename before editing.\r\n"
               "\r\n"
               "  Arrows            Move the cursor.\r\n"
               "  Home / End        Move to the start or end of the line.\r\n"
               "  PgUp / PgDn       Move one screen up or down.\r\n"
               "  Backspace / Del   Delete left or right; join lines at the edge.\r\n"
               "  Ctrl+S            Save to the current filename on SD.\r\n"
               "  Ctrl+Q / Esc      Return to BASIC>. Unsaved edits stay in RAM.\r\n"
               "\r\n"
               "At BASIC> and INPUT, arrows edit the current line.\r\n"
               "Ctrl+U clears it. At BASIC>, Up recalls the last command.\r\n");
    } else if (help_topic(topic, "SD")) {
        say(c, "HELP SD - FILES AND DIRECTORIES\r\n"
               "\r\n"
               "  DIR [path]       List a directory.\r\n"
               "  PWD              Show the current directory.\r\n"
               "  CD path          Change the current directory.\r\n"
               "  LOAD file        Load a program into memory.\r\n"
               "  SAVE [file]      Save the program to SD.\r\n"
               "  TYPE file        Display a text file.\r\n"
               "  MKDIR path       Create a directory.\r\n"
               "  DEL file         Delete a file.\r\n"
               "\r\n"
               "  MOUNT (or SD)    Mount the card. File commands also auto-mount.\r\n"
               "  UMOUNT           Unmount before removing the card.\r\n"
               "  SDCLK [pin]      Show or select the clock pin (PC12 or PB15).\r\n"
               "\r\n"
               "Use FAT16 or FAT32. Put paths with spaces in double quotes.\r\n"
               "Example: LOAD \"My Programs/Hello.bas\"\r\n");
    } else {
        say(c, "Unknown help topic. Try HELP BASIC, HELP EDIT or HELP SD.\r\n");
    }
}
void console_run(Console *c, const ConsoleIO *io) {
    uint8_t line[CONSOLE_PATH_CAPACITY];
    memset(c, 0, sizeof(*c)); c->io = *io;
    say(c, "\r\nStructured BASIC / NUCLEO-F412ZG\r\nType HELP for commands.\r\n");
    for (;;) {
        uint16_t n;
        int status = readline(c, "BASIC> ", line, sizeof(line) - 1, &n, 1);
        if (status == 1) return;
        if (status) continue;
        line[n] = 0;
        char *verb = strip((char *)line), *argument = verb;
        while (*argument && *argument != ' ' && *argument != '\t') {
            if (*argument >= 'a' && *argument <= 'z') *argument = (char)(*argument - 'a' + 'A');
            ++argument;
        }
        if (*argument) *argument++ = 0;
        if (strcmp(verb, "BASIC") != 0 && strcmp(verb, "APPEND") != 0) argument = strip(argument);
        if (!*verb) continue;
        if (!strcmp(verb, "HELP")) {
            show_help(c, argument);
        } else if (!strcmp(verb, "RUN")) {
            size_t count;
            if (!*argument) run(c, c->source, c->length);
            else if (load(c, argument, &count)) {
                /* Normalize lone CRs for the compiler while preserving byte count. */
                for (size_t i = 0; i < count; ++i)
                    if (c->incoming[i] == '\r' && (i + 1 == count || c->incoming[i + 1] != '\n'))
                        c->incoming[i] = '\n';
                run(c, c->incoming, count);
            }
        } else if (!strcmp(verb, "BASIC")) {
            run(c, (uint8_t *)argument, strlen(argument));
        } else if (!strcmp(verb, "NEW")) {
            char absolute[CONSOLE_PATH_CAPACITY];
            if (resolve(c, argument, absolute) && replacing(c)) {
                c->length = 0; c->dirty = 0; strcpy(c->path, absolute);
            }
        } else if (!strcmp(verb, "LIST")) {
            size_t start = 0;
            for (size_t i = 0; i < c->length; ++i) {
                if (c->source[i] == '\n') { bytes(c, c->source + start, i - start); say(c, "\r\n"); start = i + 1; }
            }
            if (start < c->length) { bytes(c, c->source + start, c->length - start); say(c, "\r\n"); }
        } else if (!strcmp(verb, "APPEND")) {
            size_t count = strlen(argument);
            size_t separator = c->length && c->source[c->length - 1] != '\n' ? 1u : 0u;
            if (count + separator + 1 > sizeof(c->source) - c->length) say(c, "Source buffer full.\r\n");
            else {
                if (separator) c->source[c->length++] = '\n';
                memcpy(c->source + c->length, argument, count); c->length += count;
                c->source[c->length++] = '\n'; c->dirty = 1;
            }
        } else if (!strcmp(verb, "LOAD") || !strcmp(verb, "EDIT")) {
            int edit = !strcmp(verb, "EDIT");
            size_t count;
            char absolute[CONSOLE_PATH_CAPACITY];
            if (edit && !*argument) editor(c);
            else if (resolve(c, argument, absolute) && replacing(c) && load(c, absolute, &count)) {
                load_editor(c, count, absolute);
                if (edit) editor(c);
                else say(c, "Loaded.\r\n");
            }
        } else if (!strcmp(verb, "SAVE")) {
            save(c, *argument ? argument : c->path);
        } else if (!c->io.command || !c->io.command(c->io.context, verb, argument)) {
            say(c, "Unknown command. Use HELP or BASIC statement.\r\n");
        }
    }
}
