#include "board.h"
#include "ff.h"
#include "diskio.h"
#include <stdio.h>
#include <string.h>

static FATFS volume;
static int mounted;
static void output(const char *text) { board_send(NULL, text, strlen(text)); }
static void printable(const char *p) {
    while (*p) {
        unsigned char ch = (unsigned char)*p++;
        if (ch < 32 || ch > 126) ch = '?';
        board_send(NULL, &ch, 1);
    }
}
static int failure(const char *operation, FRESULT result) {
    char message[128];
    if (result == FR_OK) return 0;
    const char *cause;
    switch (result) {
    case FR_NOT_READY: cause = "card unavailable or SDIO initialization failed"; break;
    case FR_NO_FILESYSTEM: cause = "no FAT16/FAT32 filesystem found"; break;
    case FR_NO_FILE: cause = "file not found"; break;
    case FR_NO_PATH: cause = "directory not found"; break;
    case FR_EXIST: cause = "destination already exists"; break;
    case FR_DENIED: cause = "access denied or volume full"; break;
    case FR_INVALID_NAME: cause = "invalid name or path"; break;
    case FR_DISK_ERR: cause = "card read/write error"; break;
    default: cause = "FatFs error"; break;
    }
    (void)snprintf(message, sizeof(message), "%s: %s (%u).\r\n", operation, cause, (unsigned)result);
    output(message); return -1;
}
static void unmount(void) {
    (void)f_mount(NULL, "", 0);
    sd_reset(); mounted = 0;
}
static int mount(void) {
    if (mounted && !(disk_status(0) & STA_NOINIT)) return 0;
    unmount();
    FRESULT result = f_mount(&volume, "", 1);
    if (result != FR_OK) return failure("SD", result);
    mounted = 1; return 0;
}
static int valid_path(const char *path) {
    /* Single logical volume. Keep control characters and drive aliases out. */
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p) {
        if (*p < 32 || *p == 127 || *p == ':') {
            output("Invalid path.\r\n"); return 0;
        }
    }
    return 1;
}
int card_resolve(void *context, const char *path, char *absolute, size_t capacity) {
    char cwd[CONSOLE_PATH_CAPACITY] = "/";
    int length;
    (void)context;
    if (!valid_path(path)) return -1;
    if (*path == '/' || *path == '\\') length = snprintf(absolute, capacity, "%s", path);
    else {
        if (mounted && failure("PWD", f_getcwd(cwd, sizeof(cwd)))) return -1;
        /* FatFs may prepend the single logical drive number. The port accepts
         * paths within that volume only. */
        char *root = strchr(cwd, ':');
        root = root ? root + 1 : cwd;
        size_t n = strlen(root);
        length = snprintf(absolute, capacity, "%s%s%s", root,
                          n && root[n - 1] == '/' ? "" : "/", path);
    }
    return length < 0 || (size_t)length >= capacity ? failure("Path", FR_INVALID_NAME) : 0;
}
int card_load(void *context, const char *path, uint8_t *data, size_t capacity, size_t *length) {
    FIL file;
    UINT read = 0;
    (void)context;
    if (!valid_path(path) || mount()) return -1;
    FRESULT result = f_open(&file, path, FA_READ);
    if (result != FR_OK) return failure("LOAD", result);
    if (f_size(&file) > capacity) {
        (void)f_close(&file);
        output("LOAD: file exceeds the 16 KiB source buffer.\r\n"); return -1;
    }
    result = f_read(&file, data, (UINT)capacity, &read);
    FRESULT close = f_close(&file);
    if (result != FR_OK || close != FR_OK) return failure("LOAD", result != FR_OK ? result : close);
    *length = read; return 0;
}
int card_save(void *context, const char *path, const uint8_t *data, size_t length) {
    char temporary[CONSOLE_PATH_CAPACITY + 8], backup[CONSOLE_PATH_CAPACITY + 8];
    FIL file;
    FILINFO info = {0};
    UINT written = 0;
    (void)context;
    if (!valid_path(path) || mount()) return -1;
    if (strlen(path) >= CONSOLE_PATH_CAPACITY) return failure("SAVE", FR_INVALID_NAME);
    (void)snprintf(temporary, sizeof(temporary), "%s.sbtmp", path);
    (void)snprintf(backup, sizeof(backup), "%s.sbbak", path);
    FRESULT result = f_stat(path, &info);
    int replacing = result == FR_OK;
    if (replacing && (info.fattrib & (AM_DIR | AM_RDO))) return failure("SAVE", FR_DENIED);
    if (!replacing && result != FR_NO_FILE) return failure("SAVE", result);
    if (replacing) {
        result = f_stat(backup, &info);
        if (result != FR_NO_FILE) return failure("SAVE backup", result == FR_OK ? FR_EXIST : result);
    }
    result = f_open(&file, temporary, FA_CREATE_NEW | FA_WRITE);
    if (result != FR_OK) return failure("SAVE temporary", result);
    result = f_write(&file, data, (UINT)length, &written);
    FRESULT close = f_close(&file);
    if (result != FR_OK || written != length || close != FR_OK) {
        (void)f_unlink(temporary);
        return failure("SAVE", result != FR_OK ? result : (close != FR_OK ? close : FR_DENIED));
    }
    if (replacing) {
        result = f_rename(path, backup);
        if (result != FR_OK) { (void)f_unlink(temporary); return failure("SAVE backup", result); }
    }
    result = f_rename(temporary, path);
    if (result != FR_OK) {
        if (replacing && f_rename(backup, path) != FR_OK)
            output("SAVE: previous version retained in the .sbbak file.\r\n");
        (void)f_unlink(temporary); return failure("SAVE", result);
    }
    if (replacing && f_unlink(backup) != FR_OK) output("SAVE: previous version remains in .sbbak.\r\n");
    return 0;
}
static int directory(const char *path) {
    DIR dir;
    FRESULT result = f_opendir(&dir, path);
    if (result != FR_OK) return failure("DIR", result);
    for (;;) {
        FILINFO item = {0};
        result = f_readdir(&dir, &item);
        if (result != FR_OK || !item.fname[0]) break;
        printable(item.fname);
        if (item.fattrib & AM_DIR) output("/\r\n");
        else {
            char length[40];
            (void)snprintf(length, sizeof(length), "  %lu bytes\r\n", (unsigned long)item.fsize);
            output(length);
        }
    }
    (void)f_closedir(&dir);
    return failure("DIR", result);
}
static void type_file(const char *path) {
    FIL file;
    uint8_t buffer[256];
    FRESULT result = f_open(&file, path, FA_READ);
    if (result != FR_OK) { failure("TYPE", result); return; }
    unsigned cr = 0;
    for (;;) {
        UINT count;
        result = f_read(&file, buffer, sizeof(buffer), &count);
        if (result != FR_OK || !count) break;
        for (UINT i = 0; i < count; ++i) {
            uint8_t ch = buffer[i];
            if (ch == '\n' && cr) { cr = 0; continue; }
            cr = ch == '\r';
            if (cr || ch == '\n') output("\r\n");
            else { if (ch < 32 && ch != '\t') ch = '?'; board_send(NULL, &ch, 1); }
        }
    }
    (void)f_close(&file); output("\r\n"); failure("TYPE", result);
}
int card_command(void *context, const char *verb, const char *arg) {
    (void)context;
    if (!strcmp(verb, "SDCLK")) {
        if (*arg) {
            int pb = !strcmp(arg, "PB15") || !strcmp(arg, "pb15");
            if (!pb && strcmp(arg, "PC12") && strcmp(arg, "pc12")) {
                output("Use SDCLK PC12 or SDCLK PB15.\r\n"); return 1;
            }
            unmount(); sd_select_alternate(pb);
        }
        output(sd_alternate() ? "SDIO CLK = PB15.\r\n" : "SDIO CLK = PC12.\r\n");
    } else if (!strcmp(verb, "UMOUNT")) {
        unmount(); output("SD unmounted.\r\n");
    } else if (!strcmp(verb, "MOUNT") || !strcmp(verb, "SD")) {
        if (!mount()) output("SD mounted.\r\n");
    } else if (!strcmp(verb, "DIR") || !strcmp(verb, "CD") || !strcmp(verb, "PWD") ||
               !strcmp(verb, "TYPE") || !strcmp(verb, "MKDIR") || !strcmp(verb, "DEL")) {
        if (!valid_path(arg) || mount()) return 1;
        if (!strcmp(verb, "DIR")) directory(arg);
        else if (!strcmp(verb, "CD") || !strcmp(verb, "PWD")) {
            char cwd[CONSOLE_PATH_CAPACITY];
            FRESULT result = *arg && !strcmp(verb, "CD") ? f_chdir(arg) : FR_OK;
            if (result == FR_OK) result = f_getcwd(cwd, sizeof(cwd));
            if (!failure(verb, result)) { printable(cwd); output("\r\n"); }
        } else if (!*arg) output("A path is required.\r\n");
        else if (!strcmp(verb, "TYPE")) type_file(arg);
        else if (!strcmp(verb, "MKDIR")) {
            if (!failure("MKDIR", f_mkdir(arg))) output("Directory created.\r\n");
        } else {
            FILINFO info = {0};
            FRESULT result = f_stat(arg, &info);
            if (result == FR_OK) result = info.fattrib & AM_DIR ? FR_DENIED : f_unlink(arg);
            if (!failure("DEL", result)) output("File deleted.\r\n");
        }
    } else return 0;
    return 1;
}
