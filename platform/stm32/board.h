#ifndef SB_STM32_BOARD_H
#define SB_STM32_BOARD_H
#include "console.h"
void board_send(void *context, const void *data, size_t n);
void sd_reset(void);
void sd_select_alternate(int alternate);
int sd_alternate(void);
int card_command(void *context, const char *verb, const char *argument);
int card_load(void *context, const char *name, uint8_t *buffer, size_t capacity, size_t *length);
int card_save(void *context, const char *name, const uint8_t *buffer, size_t length);
int card_resolve(void *context, const char *name, char *absolute, size_t capacity);
#endif
