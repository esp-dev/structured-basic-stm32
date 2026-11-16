#include "board.h"
#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>

/* The VM owns its own fixed arena. Newlib needs bounded scratch allocation
 * for decimal floating-point conversion. Keep it separate from the C stack. */
static uint64_t libc_heap[8192 / sizeof(uint64_t)];
static size_t used;

void *_sbrk(ptrdiff_t increment) {
    size_t previous = used;
    if (increment >= 0 && (size_t)increment <= sizeof(libc_heap) - used) {
        used += (size_t)increment;
        return (uint8_t *)libc_heap + previous;
    }
    if (increment < 0 && increment >= -(ptrdiff_t)used) {
        used -= (size_t)-increment;
        return (uint8_t *)libc_heap + previous;
    }
    errno = ENOMEM;
    return (void *)-1;
}

int _write(int fd, const void *data, size_t n) {
    if (fd != 1 && fd != 2) { errno = EBADF; return -1; }
    if (n > INT32_MAX) { errno = EINVAL; return -1; }
    board_send(NULL, data, n); return (int)n;
}
int _read(int fd, void *data, size_t n) {
    (void)fd; (void)data; (void)n; errno = EBADF; return -1;
}
int _close(int fd) { (void)fd; errno = EBADF; return -1; }
int _isatty(int fd) { return fd >= 0 && fd <= 2; }
int _fstat(int fd, struct stat *statbuf) {
    if (!_isatty(fd)) { errno = EBADF; return -1; }
    statbuf->st_mode = S_IFCHR; return 0;
}
off_t _lseek(int fd, off_t offset, int whence) {
    (void)fd; (void)offset; (void)whence; errno = ESPIPE; return (off_t)-1;
}
int _kill(int pid, int signal) { (void)pid; (void)signal; errno = EINVAL; return -1; }
int _getpid(void) { return 1; }
