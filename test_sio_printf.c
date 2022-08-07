#include "csapp.h"
#include <stdio.h>
#include <unistd.h>
#include <limits.h>

int main(void) {
    {
        sio_dprintf(STDERR_FILENO, "%d%s%dokokokhi%%lol%d\n", 1000, "<hello>", -22333333, 0);
        sio_printf("invalid formats: %q%r%%%j%l\n%");
        sio_printf("\n%l");
        sio_printf("\nok%rdlol%r\n");
        sio_printf("char %c string %s percent %%\n", 'a', "abc");

        int big_int = INT_MIN;
        printf("int size: %d %u %x\n", big_int, big_int, big_int);

        long big_long = LONG_MIN;
        sio_printf("long size: %ld %lu %lx\n", big_long, big_long, big_long);

        size_t big_size = ((size_t) 1) << 63;
        sio_printf("size_t size: %zd %zu %zx\n", big_size, big_size, big_size);

        for (size_t i = big_size - 2; i <= big_size + 2; i++) {
            sio_printf("edge test: %zd %zu %zx\n", i, i, i);
        }

        big_size = (size_t) -1;
        sio_printf("size_t size: %zd %zu %zx\n", big_size, big_size, big_size);

        sio_printf("octal: %o %lo %zo, %o %lo %zo\n", 0, (long) 0, (size_t) 0, big_int, big_long, big_size);
        sio_printf("pointer: %p %p %p\n", NULL, (void *) 0x400640, (void *) -1);
        sio_printf("string: %s %s\n", NULL, "hola");
        sio_printf("padding:'%*d'\n", 5, 5);
        sio_printf("negative padding:'%*d'\n", -5, -5);

        sio_printf("float %f and double %lf\n", 456.1, 789.123);
        sio_printf("---------------------------------------------\n");
    }


    // Try again with real printf
    {
        fprintf(stderr, "%d%s%dokokokhi%%lol%d\n", 1000, "<hello>", -22333333, 0);
        printf("invalid formats: %q%r%%%j%l\n%");
        printf("\n%l");
        printf("\nok%rdlol%r\n");
        printf("char %c string %s percent %%\n", 'a', "abc");

        int big_int = INT_MIN;
        printf("int size: %d %u %x\n", big_int, big_int, big_int);

        long big_long = LONG_MIN;
        printf("long size: %ld %lu %lx\n", big_long, big_long, big_long);

        size_t big_size = ((size_t) 1) << 63;
        printf("size_t size: %zd %zu %zx\n", big_size, big_size, big_size);

        for (size_t i = big_size - 2; i <= big_size + 2; i++) {
            printf("edge test: %zd %zu %zx\n", i, i, i);
        }

        big_size = (size_t) -1;
        printf("size_t size: %zd %zu %zx\n", big_size, big_size, big_size);

        printf("octal: %o %lo %zo, %o %lo %zo\n", 0, (long) 0, (size_t) 0, big_int, big_long, big_size);
        printf("pointer: %p %p %p\n", NULL, (void *) 0x400640, (void *) -1);
        printf("string: %s %s\n", NULL, "hola");
        printf("padding:'%*d'\n", 5, 5);
        printf("negative padding:'%*d'\n", -5, -5);

        printf("float %f and double %lf\n", 456.1, 789.123);
        printf("---------------------------------------------\n");
    }

    return 0;
}
