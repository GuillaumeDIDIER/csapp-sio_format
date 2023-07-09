#include "csapp.h"
#include <limits.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    {
        char buffer[1024];
        char buffer4[4];
        buffer4[1] = 'g';
        buffer4[2] = 'd';
        buffer4[3] = '\0';
        ssize_t ret;

        ret = sio_snprintf(buffer, 1024, "%d", 0);
        printf("%zd:%s\n", ret, buffer);

        ret = sio_snprintf(buffer, 1024, "%*d", 5, 0);
        printf("%zd:%s\n", ret, buffer);

        ret = sio_snprintf(buffer4, 1, "%d", 0);
        printf("%zd:%s:%s\n", ret, buffer4, buffer4 + 1);

        ret = sio_snprintf(buffer4 + 1, 0, "%d", 0);
        printf("%zd:%s\n", ret, buffer4 + 1);

        ret = sio_snprintf(buffer, 1024, "%d%s%dokokokhi%%lol%d\n", 1000,
                           "<hello>", -22333333, 0);
        printf("%zd:%s\n", ret, buffer);
        ret = sio_snprintf(buffer, 1024, "invalid formats: %q%r%%%j%l\n%");
        printf("%zd:%s\n", ret, buffer);
        ret = sio_snprintf(buffer, 1024, "\n%l");
        printf("%zd:%s\n", ret, buffer);
        ret = sio_snprintf(buffer, 1024, "\nok%rdlol%r\n");
        printf("%zd:%s\n", ret, buffer);
        ret = sio_snprintf(buffer, 1024, "char %c string %s percent %%\n", 'a',
                           "abc");
        printf("%zd:%s\n", ret, buffer);

        int big_int = INT_MIN;
        ret = sio_snprintf(buffer, 1024, "int size: %d %u %x\n", big_int,
                           big_int, big_int);
        printf("%zd:%s\n", ret, buffer);

        long big_long = LONG_MIN;
        ret = sio_snprintf(buffer, 1024, "long size: %ld %lu %lx\n", big_long,
                           big_long, big_long);
        printf("%zd:%s\n", ret, buffer);

        size_t big_size = ((size_t)1) << 63;
        ret = sio_snprintf(buffer, 1024, "size_t size: %zd %zu %zx\n", big_size,
                           big_size, big_size);
        printf("%zd:%s\n", ret, buffer);

        for (size_t i = big_size - 2; i <= big_size + 2; i++) {
            ret =
                sio_snprintf(buffer, 1024, "edge test: %zd %zu %zx\n", i, i, i);
            printf("%zd:%s\n", ret, buffer);
        }

        big_size = (size_t)-1;
        ret = sio_snprintf(buffer, 1024, "size_t size: %zd %zu %zx\n", big_size,
                           big_size, big_size);
        printf("%zd:%s\n", ret, buffer);

        ret = sio_snprintf(buffer, 1024, "octal: %o %lo %zo, %o %lo %zo\n", 0,
                           (long)0, (size_t)0, big_int, big_long, big_size);
        printf("%zd:%s\n", ret, buffer);
        ret = sio_snprintf(buffer, 1024, "pointer: %p %p %p\n", NULL,
                           (void *)0x400640, (void *)-1);
        printf("%zd:%s\n", ret, buffer);
        ret = sio_snprintf(buffer, 1024, "string: %s %s\n", NULL, "hola");
        printf("%zd:%s\n", ret, buffer);
        printf("---------------------------------------------\n");
    }

    // Try again with real snprintf
    {
        char buffer[1024];
        char buffer4[4];
        buffer4[1] = 'g';
        buffer4[2] = 'd';
        buffer4[3] = '\0';
        int ret;

        ret = snprintf(buffer, 1024, "%d", 0);
        printf("%d:%s\n", ret, buffer);

        ret = snprintf(buffer, 1024, "%*d", 5, 0);
        printf("%d:%s\n", ret, buffer);

        ret = snprintf(buffer4, 1, "%d", 0);
        printf("%d:%s:%s\n", ret, buffer4, buffer4 + 1);

        ret = snprintf(buffer4, 0, "%d", 0);
        printf("%d:%s\n", ret, buffer4 + 1);

        ret = snprintf(buffer, 1024, "%d%s%dokokokhi%%lol%d\n", 1000, "<hello>",
                       -22333333, 0);
        printf("%d:%s\n", ret, buffer);
        ret = snprintf(buffer, 1024, "invalid formats: %q%r%%%j%l\n%");
        printf("%d:%s\n", ret, buffer);
        ret = snprintf(buffer, 1024, "\n%l");
        printf("%d:%s\n", ret, buffer);
        ret = snprintf(buffer, 1024, "\nok%rdlol%r\n");
        printf("%d:%s\n", ret, buffer);
        ret = snprintf(buffer, 1024, "char %c string %s percent %%\n", 'a',
                       "abc");
        printf("%d:%s\n", ret, buffer);

        int big_int = INT_MIN;
        ret = snprintf(buffer, 1024, "int size: %d %u %x\n", big_int, big_int,
                       big_int);
        printf("%d:%s\n", ret, buffer);

        long big_long = LONG_MIN;
        ret = snprintf(buffer, 1024, "long size: %ld %lu %lx\n", big_long,
                       big_long, big_long);
        printf("%d:%s\n", ret, buffer);

        size_t big_size = ((size_t)1) << 63;
        ret = snprintf(buffer, 1024, "size_t size: %zd %zu %zx\n", big_size,
                       big_size, big_size);
        printf("%d:%s\n", ret, buffer);

        for (size_t i = big_size - 2; i <= big_size + 2; i++) {
            ret = snprintf(buffer, 1024, "edge test: %zd %zu %zx\n", i, i, i);
            printf("%d:%s\n", ret, buffer);
        }

        big_size = (size_t)-1;
        ret = snprintf(buffer, 1024, "size_t size: %zd %zu %zx\n", big_size,
                       big_size, big_size);
        printf("%d:%s\n", ret, buffer);

        ret = snprintf(buffer, 1024, "octal: %o %lo %zo, %o %lo %zo\n", 0,
                       (long)0, (size_t)0, big_int, big_long, big_size);
        printf("%d:%s\n", ret, buffer);
        ret = snprintf(buffer, 1024, "pointer: %p %p %p\n", NULL,
                       (void *)0x400640, (void *)-1);
        printf("%d:%s\n", ret, buffer);
        ret = snprintf(buffer, 1024, "string: %s %s\n", NULL, "hola");
        printf("%d:%s\n", ret, buffer);
        printf("---------------------------------------------\n");
    }

    return 0;
}
