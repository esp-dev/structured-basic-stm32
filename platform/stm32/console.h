#ifndef SB_STM32_CONSOLE_H
#define SB_STM32_CONSOLE_H

#include "sbasic.h"

#define CONSOLE_SOURCE_CAPACITY 16384u
#define CONSOLE_PATH_CAPACITY 192u

typedef struct {
    void *context;
    int (*receive)(void *context, uint32_t timeout_ms);
    int (*break_requested)(void *context);
    void (*send)(void *context, const void *bytes, size_t length);
    int (*command)(void *context, const char *verb, const char *argument);
    int (*load)(void *context, const char *path, uint8_t *buffer, size_t capacity, size_t *length);
    int (*save)(void *context, const char *path, const uint8_t *buffer, size_t length);
    int (*resolve)(void *context, const char *path, char *absolute, size_t capacity);
    int (*device)(void *context, uint8_t operation, int32_t a, int32_t b, int32_t *result);
} ConsoleIO;

typedef struct {
    ConsoleIO io;
    SBasic basic;
    uint8_t source[CONSOLE_SOURCE_CAPACITY];
    uint8_t incoming[CONSOLE_SOURCE_CAPACITY];
    size_t length;
    char path[CONSOLE_PATH_CAPACITY];
    char history[CONSOLE_PATH_CAPACITY];
    uint8_t skip_lf;
    uint8_t dirty;
} Console;

void console_run(Console *console, const ConsoleIO *io);

#endif
