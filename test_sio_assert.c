#include "csapp.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static int blah(void) {
    sio_assert(0);
    printf("%u\n", 1 + (sio_assert(2 + 2 == 5), 0));
}

int main(void) {
    return blah();
}
