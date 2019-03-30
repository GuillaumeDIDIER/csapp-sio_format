/*
 * csapp.h - prototypes and definitions for the CS:APP3e book
 */

/*
 * The wrapper functions in this file, which use unix_error, are technically
 * not async-signal-safe due to the use of strerror. If this is not suitable,
 * you should write your own wrapper functions.
 *
 * For most cases, however, we consider this to be only a minor caveat. The
 * only situation that results in a lack of safety is quite unlikely: the
 * failure of a wrapper function (which will terminate the program anyway)
 * must interrupt a function that is holding the same locks as strerror.
 * Since this is a rare and exceptional condition, we consider it acceptable
 * to use these wrapper functions inside of signal handlers.
 */

#ifndef __CSAPP_H__
#define __CSAPP_H__

#define _XOPEN_SOURCE 700

#include <stddef.h>                     /* ssize_t */
#include <stdarg.h>                     /* va_list */
#include <semaphore.h>                  /* sem_t */
#include <sys/types.h>                  /* struct sockaddr */
#include <sys/socket.h>                 /* struct sockaddr */

/* Default file permissions are DEF_MODE & ~DEF_UMASK */
#define DEF_MODE   S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH
#define DEF_UMASK  S_IWGRP|S_IWOTH

/* Simplifies calls to bind(), connect(), and accept() */
typedef struct sockaddr SA;

/* Persistent state for the robust I/O (Rio) package */
#define RIO_BUFSIZE 8192
typedef struct {
    int rio_fd;                /* Descriptor for this internal buf */
    ssize_t rio_cnt;           /* Unread bytes in internal buf */
    char *rio_bufptr;          /* Next unread byte in internal buf */
    char rio_buf[RIO_BUFSIZE]; /* Internal buffer */
} rio_t;

/* External variables */
extern int h_errno;    /* Defined by BIND for DNS errors */
extern char **environ; /* Defined by libc */
extern char *__progname;

/* Misc constants */
#define MAXLINE  8192  /* Max text line length */
#define MAXBUF   8192  /* Max I/O buffer size */
#define LISTENQ  1024  /* Second argument to listen() */

/* Sio (Signal-safe I/O) routines */
ssize_t sio_printf(const char *fmt, ...)
  __attribute__ ((format (printf, 1, 2)));
ssize_t sio_fprintf(int fileno, const char *fmt, ...)
  __attribute__ ((format (printf, 2, 3)));
ssize_t sio_vfprintf(int fileno, const char *fmt, va_list argp)
  __attribute__ ((format (printf, 2, 0)));

#define sio_assert(expr) \
    ((expr) ? \
     (void) 0 : \
     __sio_assert_fail(#expr, __FILE__, __LINE__, __func__))

void __sio_assert_fail(const char *assertion, const char *file,
                       unsigned int line, const char *function)
                       __attribute__ ((noreturn));

/* POSIX semaphore wrappers */
void P(sem_t *sem);
void V(sem_t *sem);

/* Rio (Robust I/O) package */
ssize_t rio_readn(int fd, void *usrbuf, size_t n);
ssize_t rio_writen(int fd, void *usrbuf, size_t n);
void rio_readinitb(rio_t *rp, int fd);
ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n);
ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen);

/* Reentrant protocol-independent client/server helpers */
int open_clientfd(char *hostname, char *port);
int open_listenfd(char *port);

#endif /* __CSAPP_H__ */
