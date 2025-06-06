/**
 * @file csapp.c
 * @brief Functions for the CS:APP3e book
 */

#include "csapp.h"

#ifdef CSAPP_HAS_DTOA
#include "csapp_dtoa.h"
#endif // CSAPP_HAS_DTOA

#include <errno.h>      /* errno */
#include <limits.h>     /* SSIZE_MAX */
#include <netdb.h>      /* freeaddrinfo() */
#include <semaphore.h>  /* sem_t */
#include <signal.h>     /* struct sigaction */
#include <stdarg.h>     /* va_list */
#include <stdbool.h>    /* bool */
#include <stddef.h>     /* ssize_t */
#include <stdint.h>     /* intmax_t */
#include <stdio.h>      /* stderr */
#include <stdlib.h>     /* abort() */
#include <string.h>     /* memset() */
#include <sys/socket.h> /* struct sockaddr */
#include <sys/types.h>  /* struct sockaddr */
#include <unistd.h>     /* STDIN_FILENO */

/************************************
 * Wrappers for Unix signal functions
 ***********************************/

/**
 * @brief   Wrapper for the new sigaction interface. Exits on error.
 * @param signum    Signal to set handler for.
 * @param handler   Handler function.
 *
 * @return  Previous disposition of the signal.
 */
handler_t *Signal(int signum, handler_t *handler) {
    struct sigaction action, old_action;

    action.sa_handler = handler;
    sigemptyset(&action.sa_mask); /* Block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* Restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0) {
        perror("Signal error");
        exit(1);
    }

    return old_action.sa_handler;
}

/*************************************************************
 * The Sio (Signal-safe I/O) package - simple reentrant output
 * functions that are safe for signal handlers.
 *************************************************************/

/* Private sio functions */

/* sio_reverse - Reverse a string (from K&R) */
static void sio_reverse(char s[], size_t len) {
    size_t i, j;
    for (i = 0, j = len - 1; i < j; i++, j--) {
        char c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

/* write_digits - write digit values of v in base b to string */
static size_t write_digits(uintmax_t v, char s[], unsigned char b) {
    size_t i = 0;
    do {
        unsigned char c = v % b;
        if (c < 10) {
            s[i++] = (char)(c + '0');
        } else {
            s[i++] = (char)(c - 10 + 'a');
        }
    } while ((v /= b) > 0);
    return i;
}

/* Based on K&R itoa() */
/* intmax_to_string - Convert an intmax_t to a base b string */
static size_t intmax_to_string(intmax_t v, char s[], unsigned char b) {
    bool neg = v < 0;
    size_t len;

    if (neg) {
        len = write_digits((uintmax_t)-v, s, b);
        s[len++] = '-';
    } else {
        len = write_digits((uintmax_t)v, s, b);
    }

    s[len] = '\0';
    sio_reverse(s, len);
    return len;
}

/* uintmax_to_string - Convert a uintmax_t to a base b string */
static size_t uintmax_to_string(uintmax_t v, char s[], unsigned char b) {
    size_t len = write_digits(v, s, b);
    s[len] = '\0';
    sio_reverse(s, len);
    return len;
}

/* Public Sio functions */

/**
 * @brief   Prints formatted output to stdout.
 * @param fmt   The format string used to determine the output.
 * @param ...   The arguments for the format string.
 * @return      The number of bytes written, or -1 on error.
 *
 * @remark   This function is async-signal-safe.
 * @see      sio_vdprintf
 */
ssize_t sio_printf(const char *fmt, ...) {
    va_list argp;
    va_start(argp, fmt);
    ssize_t ret = sio_vdprintf(STDOUT_FILENO, fmt, argp);
    va_end(argp);
    return ret;
}

/**
 * @brief   Prints formatted output to a file descriptor.
 * @param fileno   The file descriptor to print output to.
 * @param fmt      The format string used to determine the output.
 * @param ...      The arguments for the format string.
 * @return         The number of bytes written, or -1 on error.
 *
 * @remark   This function is async-signal-safe.
 * @see      sio_vdprintf
 */
ssize_t sio_dprintf(int fileno, const char *fmt, ...) {
    va_list argp;
    va_start(argp, fmt);
    ssize_t ret = sio_vdprintf(fileno, fmt, argp);
    va_end(argp);
    return ret;
}

/**
 * @brief   Prints formatted output to STDERR_FILENO.
 * @param fmt      The format string used to determine the output.
 * @param ...      The arguments for the format string.
 * @return         The number of bytes written, or -1 on error.
 *
 * @remark   This function is async-signal-safe.
 * @see      sio_vdprintf
 */
ssize_t sio_eprintf(const char *fmt, ...) {
    va_list argp;
    va_start(argp, fmt);
    ssize_t ret = sio_vdprintf(STDERR_FILENO, fmt, argp);
    va_end(argp);
    return ret;
}

struct _format_data {
    const char *str; // String to output
    size_t len;      // Length of string to output
    char buf[128];   // Backing buffer to use for conversions
};

/**
 * @brief   Prints formatted output to a file descriptor from a va_list.
 * @param fileno   The file descriptor to print output to.
 * @param fmt      The format string used to determine the output.
 * @param argp     The arguments for the format string.
 * @return         The number of bytes written, or -1 on error.
 *
 * @remark   This function is async-signal-safe.
 *
 * This is a reentrant and async-signal-safe implementation of vdprintf, used
 * to implement the associated formatted sio functions.
 *
 * This function writes directly to a file descriptor (using the `rio_writen`
 * function from csapp), as opposed to a `FILE *` from the standard library.
 * However, since these writes are unbuffered, this is not very efficient, and
 * should only be used when async-signal-safety is necessary.
 *
 * The only supported format specifiers are the following:
 *  -  Int types: %d, %i, %u, %x, %o (with size specifiers l, z)
 *  -  Others: %c, %s, %%, %p
 */
ssize_t sio_vdprintf(int fileno, const char *fmt, va_list argp) {
    sio_write_output_t state;
    state.fileno = fileno;
    return sio_vformat(sio_write_output, &state, fmt, argp);
}

ssize_t sio_snprintf(char *str, size_t size, const char *fmt, ...) {
    va_list argp;
    va_start(argp, fmt);
    ssize_t ret = sio_vsnprintf(str, size, fmt, argp);
    va_end(argp);
    return ret;
}

ssize_t sio_vsnprintf(char *str, size_t size, const char *fmt, va_list argp) {
    sio_buffer_output_t state;
    ssize_t ret;
    if (size > 0) {
        state.buffer = str;
        state.remaining = size - 1;
    } else { // No Output
        state.buffer = NULL;
        state.remaining = 0;
    }
    ret = sio_vformat(sio_buffer_output, &state, fmt, argp);
    if (ret >= 0 && state.buffer != NULL) {
        *(state.buffer) = '\0';
    }
    return ret;
}

#define PADDING_BUF_LEN 128

ssize_t sio_write_output(void *state, char padding, size_t count_left, size_t count_right,
                         const char *data, size_t len) {
    int fileno = ((sio_write_output_t *)state)->fileno;

    if (count_left > (size_t)SSIZE_MAX || len > (size_t) SSIZE_MAX || count_left + len > (size_t) SSIZE_MAX || count_right > (size_t) SSIZE_MAX || count_left + len + count_right > (size_t) SSIZE_MAX) {
        return -1;
    }
    ssize_t num_written = 0;

    char buf[PADDING_BUF_LEN];
    memset(buf, padding, PADDING_BUF_LEN);
    while ((size_t) num_written < count_left) {
        size_t padding_left_len = count_left - (size_t) num_written;
        if (padding_left_len > PADDING_BUF_LEN) {
            padding_left_len = PADDING_BUF_LEN;
        }
        ssize_t ret = rio_writen(fileno, (const void *)buf, padding_left_len);
        if (ret < 0 || (size_t)ret != padding_left_len) {
            return -1;
        }
        num_written += ret;
    }

    if (len > 0) {
        ssize_t ret = rio_writen(fileno, (const void *)data, len);
        if (ret < 0 || (size_t)ret != len) {
            return -1;
        }
        num_written += ret;
    }
    while ((size_t) num_written < count_left + len + count_right) {
        size_t padding_right_len = count_left + len + count_right - (size_t) num_written;
        if (padding_right_len > PADDING_BUF_LEN) {
            padding_right_len = PADDING_BUF_LEN;
        }
        ssize_t ret = rio_writen(fileno, (const void *)buf, padding_right_len);
        if (ret < 0 || (size_t)ret != padding_right_len) {
            return -1;
        }
        num_written += ret;
    }
    return num_written;
}

ssize_t sio_buffer_output(void *state, char padding, size_t count_left, size_t count_right,
                          const char *data, size_t len) {

    size_t i = 0;
    size_t total_len = count_left + len + count_right; // TODO check all those null bytes
    if (total_len > SSIZE_MAX) { // This check is in general insufficient,
                                 // but in our use case,
                                 // one of the three number is 0.
        return -1;
    }
    sio_buffer_output_t *buffer_state = state;

    if (buffer_state->buffer != NULL) {
        for (i = 0; i < count_left && buffer_state->remaining > 0; i++) {
            *(buffer_state->buffer) = padding;
            buffer_state->buffer++;
            buffer_state->remaining--;
        }
        for (i = 0; i < len && buffer_state->remaining > 0; i++) {
            *(buffer_state->buffer) = *data;
            buffer_state->buffer++;
            data++;
            buffer_state->remaining--;
        }
        for (i = 0; i < count_right && buffer_state->remaining > 0; i++) {
            *(buffer_state->buffer) = padding;
            buffer_state->buffer++;
            buffer_state->remaining--;
        }
        *(buffer_state->buffer) = '\0';
    }
    return (ssize_t)total_len;
}

ssize_t sio_format(sio_output_function output, void *output_state,
                   const char *fmt, ...) {
    va_list argp;
    va_start(argp, fmt);
    ssize_t ret = sio_vformat(output, output_state, fmt, argp);
    va_end(argp);
    return ret;
}

typedef enum {
    NumSizeInt,
    NumSizeLong,
    NumSizeLongLong,
    NumSizeSize,
} number_size_t;

/*typedef enum {
    NumNone,
    NumUnsigned,
    NumSigned,
    NumFloat,
} number_type_t;*/

/* TODO's: Add support for .* precision, and refactor the name num_written below
 */
ssize_t sio_vformat(sio_output_function output, void *output_state,
                    const char *fmt, va_list argp) {
    size_t pos = 0;
    ssize_t num_written =
        0; // refactor this name, which no longer reflects the real meaning

    bool error = false;
    while (fmt[pos] != '\0') {
        // Int to string conversion
        struct _format_data data;
        memset(&data, 0, sizeof(data));

        const char *local_fmt = &fmt[pos];

        size_t local_pos = 0;
        bool handled = false;
        ssize_t written = 0;
        bool padded = false;
        bool precision_given = false;
        int padding = 0;
        int precision = -1;
        size_t current = 0;
        number_size_t num_size = NumSizeInt;
        // number_type_t num_type = NumNone;

        if (local_fmt[0] == '%' && local_fmt[1] != '\0') {
            current += 1;
            // Marked if we need to convert an integer
            char convert_type = '\0';
            union {
                uintmax_t u;
                intmax_t s;
                double f;
            } convert_value = {.u = 0};

            if (local_fmt[current] == '*') {
                if (local_fmt[current + 1] != '\0'){
                    padded = true;
                    padding = va_arg(argp, int);
                    current++;
                } else {
                    error = true;
                }
            }

            if (local_fmt[current] == '.') {
                if (local_fmt[current+1] == '*' && local_fmt[current] + 2 != '\0') {
                    precision = va_arg(argp, int);
                    if (precision >= 0) {
                        precision_given = true;
                    } // TODO, wire up correct usage and default value for types other than s and f.
                    current+=2;
                } else {
                    error = true;
                }
            }


            switch (local_fmt[current]) { // FIXME,
                                          // risk of reading out of bounds
            case 'l':
                if (local_fmt[current+1] != '\0') {
                    current++;
                    if (local_fmt[current] == 'l') {
                        if (local_fmt[current+1] != '\0') {
                            num_size = NumSizeLongLong;
                            current++;
                        } else {
                            error = true;
                        }
                    } else {
                        num_size = NumSizeLong;
                    }
                } else {
                    error = true;
                }
                break;
            case 'z':
                if (local_fmt[current+1] != '\0') {
                current++;
                num_size = NumSizeSize;
                } else {
                    error = true;
                }
                break;
            } // add j for intmax_t ? t is ptr_diff ?

            switch (local_fmt[current]) {
                // Character format
            case 'c': {
                if (num_size != NumSizeInt) {
                    error = true;
                } else {
                    data.buf[0] = (char)va_arg(argp, int);
                    data.buf[1] = '\0';
                    data.str = data.buf;
                    data.len = 1;
                    handled = true;
                    current++;
                    local_pos += current;
                }
                break;
            }

                // String format
            case 's': {
                if (num_size != NumSizeInt) {
                    error = true;
                } else {
                    data.str = va_arg(argp, char *);
                    if (data.str == NULL) {
                        data.str = "(null)";
                    }
                    data.len = strlen(data.str);
                    if (precision_given && precision < data.len) {
                        data.len = precision;
                    }
                    handled = true;
                    current++;
                    local_pos += current;
                }
                break;
            }

                // Escaped %
            case '%': {
                if (current != 1) {
                    error = true;
                } else {
                    data.str = local_fmt;
                    data.len = 1;
                    handled = true;
                    current++;
                    local_pos += current;
                }
                break;
            }

                // Pointer type
            case 'p': {
                if (num_size != NumSizeInt) {
                    error = true;
                } else {
                    void *ptr = va_arg(argp, void *);
                    if (ptr == NULL) {
                        data.str = "(nil)";
                        data.len = strlen(data.str);
                        handled = true;
                    } else {
                        convert_type = 'p';
                        convert_value.u = (uintmax_t)(uintptr_t)ptr;
                    }
                    current++;
                    local_pos += current;
                }
                break;
            }

                // Int types with no format specifier
            case 'd':
            case 'i': {
                // num_type = NumSigned;
                convert_type = 'd';
                current++;

                // convert_value.s = (intmax_t)va_arg(argp, int);
                switch (num_size) {
                case NumSizeInt:
                    convert_value.s = (intmax_t)va_arg(argp, int);
                    break;
                case NumSizeLong:
                    convert_value.s = (intmax_t)va_arg(argp, long int);
                    break;
                case NumSizeLongLong: // Need to add #ifdef checks ?
                    convert_value.s = (intmax_t)va_arg(argp, long long int);
                    break;
                case NumSizeSize:
                    convert_value.s = (intmax_t)va_arg(argp, ssize_t);
                    break;
                default:
                    // internal error
                    __sio_assert_fail("Unknown Number Size in format", __FILE__,
                                      __LINE__, __func__);
                    // break;
                }
                local_pos += current;
                break;
            }
            case 'u':
            case 'x':
            case 'o': {
                // num_type = NumUnsigned;
                convert_type = local_fmt[current];
                current++;
                switch (num_size) {
                case NumSizeInt:
                    convert_value.u = (uintmax_t)va_arg(argp, unsigned);
                    break;
                case NumSizeLong:
                    convert_value.u = (uintmax_t)va_arg(argp, unsigned long);
                    break;
                case NumSizeLongLong:
                    convert_value.u =
                        (uintmax_t)va_arg(argp, unsigned long long);
                    break;
                case NumSizeSize:
                    convert_value.u = (uintmax_t)va_arg(argp, size_t);
                    break;
                }
                local_pos += current;
                break;
            }
            case 'f': { // Default float precision is 6
                // num_type = NumFloat;
                current++;
                switch (num_size) {
                case NumSizeInt:
                    convert_type = 'f';
                    convert_value.f = va_arg(argp, double);
                    break;
                case NumSizeLong:
                    convert_type = 'f';
                    convert_value.f = va_arg(argp, double);
                    break;
                default:
                    // User error;
                    error = true;
                    break;
                }
                local_pos += current;
                break;
            }
            default:
                error = true;
                break;
            }

            // Convert int type to string
            switch (convert_type) {
            case 'd':
                data.str = data.buf;
                data.len = intmax_to_string(convert_value.s, data.buf, 10);
                handled = true;
                break;
            case 'u':
                data.str = data.buf;
                data.len = uintmax_to_string(convert_value.u, data.buf, 10);
                handled = true;
                break;
            case 'x':
                data.str = data.buf;
                data.len = uintmax_to_string(convert_value.u, data.buf, 16);
                handled = true;
                break;
            case 'o':
                data.str = data.buf;
                data.len = uintmax_to_string(convert_value.u, data.buf, 8);
                handled = true;
                break;
            case 'p':
                strcpy(data.buf, "0x");
                data.str = data.buf;
                data.len =
                    uintmax_to_string(convert_value.u, data.buf + 2, 16) + 2;
                handled = true;
                break;
            case 'f':
                // Float may generate longer results than 128
                data.str = data.buf;
#ifdef CSAPP_HAS_DTOA
                data.len = 0;
                if (!precision_given) {
                    precision = FLOAT_DEFAULT_PRECISION;
                }
                written = sio_format_double_exact(output, output_state,
                                                  convert_value.f, FORMAT_f,
                                                  padding, precision); // Right padding is unsupported
#else
                data.str = "<float>";
                data.len = strlen(data.str);
#endif // CSAPP_HAS_DTOA
                handled = true;
                break;
            default:
                error = true;
            }

        }

        // Didn't match a format above
        // Handle block of non-format characters
        if (!handled) {
            data.str = local_fmt;
            data.len = 1 + strcspn(local_fmt + 1, "%");
            local_pos += data.len;
        }

        // Handle format characters
        pos += local_pos;

        // Write output
        size_t right_padding_count = 0;
        size_t left_padding_count = 0;
        if (padded && padding > 0 && (size_t) padding > data.len) { // Right padding unsupported
            left_padding_count = (size_t) padding - data.len;
        }
        if (padded && padding < 0 && (size_t) (-padding) > data.len) { // Right padding unsupported
            right_padding_count = (size_t) (-padding) - data.len;
        }
        if (written == 0) {
            written =
                output(output_state, ' ', left_padding_count, right_padding_count, data.str, data.len);
        }
        if (written < 0) {
            return -1;
        }
        num_written += written;
    }

    if (error) {
        return -1;
    }

    return num_written;
}

/* Async-signal-safe assertion support*/
void __sio_assert_fail(const char *assertion, const char *file,
                       unsigned int line, const char *function) {
    sio_dprintf(STDERR_FILENO, "%s: %s:%u: %s: Assertion `%s' failed.\n",
                __progname, file, line, function, assertion);
    abort();
}

/****************************************
 * The Rio package - Robust I/O functions
 ****************************************/

/*
 * rio_readn - Robustly read n bytes (unbuffered)
 */
ssize_t rio_readn(int fd, void *usrbuf, size_t n) {
    size_t nleft = n;
    ssize_t nread;
    char *bufp = usrbuf;

    while (nleft > 0) {
        if ((nread = read(fd, bufp, nleft)) < 0) {
            if (errno != EINTR) {
                return -1; /* errno set by read() */
            }

            /* Interrupted by sig handler return, call read() again */
            nread = 0;
        } else if (nread == 0) {
            break; /* EOF */
        }
        nleft -= (size_t)nread;
        bufp += nread;
    }
    return (ssize_t)(n - nleft); /* Return >= 0 */
}

/*
 * rio_writen - Robustly write n bytes (unbuffered)
 */
ssize_t rio_writen(int fd, const void *usrbuf, size_t n) {
    size_t nleft = n;
    ssize_t nwritten;
    const char *bufp = usrbuf;

    while (nleft > 0) {
        if ((nwritten = write(fd, bufp, nleft)) <= 0) {
            if (errno != EINTR) {
                return -1; /* errno set by write() */
            }

            /* Interrupted by sig handler return, call write() again */
            nwritten = 0;
        }
        nleft -= (size_t)nwritten;
        bufp += nwritten;
    }
    return (ssize_t)n;
}

/*
 * rio_read - This is a wrapper for the Unix read() function that
 *    transfers min(n, rio_cnt) bytes from an internal buffer to a user
 *    buffer, where n is the number of bytes requested by the user and
 *    rio_cnt is the number of unread bytes in the internal buffer. On
 *    entry, rio_read() refills the internal buffer via a call to
 *    read() if the internal buffer is empty.
 */
static ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n) {
    size_t cnt;

    while (rp->rio_cnt <= 0) { /* Refill if buf is empty */
        rp->rio_cnt = read(rp->rio_fd, rp->rio_buf, sizeof(rp->rio_buf));
        if (rp->rio_cnt < 0) {
            if (errno != EINTR) {
                return -1; /* errno set by read() */
            }

            /* Interrupted by sig handler return, nothing to do */
        } else if (rp->rio_cnt == 0) {
            return 0; /* EOF */
        } else {
            rp->rio_bufptr = rp->rio_buf; /* Reset buffer ptr */
        }
    }

    /* Copy min(n, rp->rio_cnt) bytes from internal buf to user buf */
    cnt = n;
    if ((size_t)rp->rio_cnt < n) {
        cnt = (size_t)rp->rio_cnt;
    }
    memcpy(usrbuf, rp->rio_bufptr, cnt);
    rp->rio_bufptr += cnt;
    rp->rio_cnt -= cnt;
    return (ssize_t)cnt;
}

/*
 * rio_readinitb - Associate a descriptor with a read buffer and reset buffer
 */
void rio_readinitb(rio_t *rp, int fd) {
    rp->rio_fd = fd;
    rp->rio_cnt = 0;
    rp->rio_bufptr = rp->rio_buf;
}

/*
 * rio_readnb - Robustly read n bytes (buffered)
 */
ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n) {
    size_t nleft = n;
    ssize_t nread;
    char *bufp = usrbuf;

    while (nleft > 0) {
        if ((nread = rio_read(rp, bufp, nleft)) < 0) {
            return -1; /* errno set by read() */
        } else if (nread == 0) {
            break; /* EOF */
        }
        nleft -= (size_t)nread;
        bufp += nread;
    }
    return (ssize_t)(n - nleft); /* return >= 0 */
}

/*
 * rio_readlineb - Robustly read a text line (buffered)
 */
ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen) {
    size_t n;
    ssize_t rc;
    char c, *bufp = usrbuf;

    for (n = 1; n < maxlen; n++) {
        if ((rc = rio_read(rp, &c, 1)) == 1) {
            *bufp++ = c;
            if (c == '\n') {
                n++;
                break;
            }
        } else if (rc == 0) {
            if (n == 1) {
                return 0; /* EOF, no data read */
            } else {
                break; /* EOF, some data was read */
            }
        } else {
            return -1; /* Error */
        }
    }
    *bufp = 0;
    return (ssize_t)(n - 1);
}

/********************************
 * Client/server helper functions
 ********************************/
/*
 * open_clientfd - Open connection to server at <hostname, port> and
 *     return a socket descriptor ready for reading and writing. This
 *     function is reentrant and protocol-independent.
 *
 *     On error, returns:
 *       -2 for getaddrinfo error
 *       -1 with errno set for other errors.
 */
int open_clientfd(const char *hostname, const char *port) {
    int clientfd = -1, rc;
    struct addrinfo hints, *listp, *p;

    /* Get a list of potential server addresses */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_STREAM; /* Open a connection */
    hints.ai_flags = AI_NUMERICSERV; /* ... using a numeric port arg. */
    hints.ai_flags |= AI_ADDRCONFIG; /* Recommended for connections */
    if ((rc = getaddrinfo(hostname, port, &hints, &listp)) != 0) {
        fprintf(stderr, "getaddrinfo failed (%s:%s): %s\n", hostname, port,
                gai_strerror(rc));
        return -2;
    }

    /* Walk the list for one that we can successfully connect to */
    for (p = listp; p; p = p->ai_next) {
        /* Create a socket descriptor */
        clientfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (clientfd < 0) {
            continue; /* Socket failed, try the next */
        }

        /* Connect to the server */
        if (connect(clientfd, p->ai_addr, p->ai_addrlen) != -1) {
            break; /* Success */
        }

        /* Connect failed, try another */
        if (close(clientfd) < 0) {
            fprintf(stderr, "open_clientfd: close failed: %s\n",
                    strerror(errno));
            return -1;
        }
    }

    /* Clean up */
    freeaddrinfo(listp);
    if (!p) { /* All connects failed */
        return -1;
    } else { /* The last connect succeeded */
        return clientfd;
    }
}

/*
 * open_listenfd - Open and return a listening socket on port. This
 *     function is reentrant and protocol-independent.
 *
 *     On error, returns:
 *       -2 for getaddrinfo error
 *       -1 with errno set for other errors.
 */
int open_listenfd(const char *port) {
    struct addrinfo hints, *listp, *p;
    int listenfd = -1, rc, optval = 1;

    /* Get a list of potential server addresses */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_STREAM;             /* Accept connections */
    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG; /* ... on any IP address */
    hints.ai_flags |= AI_NUMERICSERV;            /* ... using port number */
    if ((rc = getaddrinfo(NULL, port, &hints, &listp)) != 0) {
        fprintf(stderr, "getaddrinfo failed (port %s): %s\n", port,
                gai_strerror(rc));
        return -2;
    }

    /* Walk the list for one that we can bind to */
    for (p = listp; p; p = p->ai_next) {
        /* Create a socket descriptor */
        listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listenfd < 0) {
            continue; /* Socket failed, try the next */
        }

        /* Eliminates "Address already in use" error from bind */
        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval,
                   sizeof(int));

        /* Bind the descriptor to the address */
        if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0) {
            break; /* Success */
        }

        if (close(listenfd) < 0) { /* Bind failed, try the next */
            fprintf(stderr, "open_listenfd close failed: %s\n",
                    strerror(errno));
            return -1;
        }
    }

    /* Clean up */
    freeaddrinfo(listp);
    if (!p) { /* No address worked */
        return -1;
    }

    /* Make it a listening socket ready to accept connection requests */
    if (listen(listenfd, LISTENQ) < 0) {
        close(listenfd);
        return -1;
    }
    return listenfd;
}
