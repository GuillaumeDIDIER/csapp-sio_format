//
// Created by Guillaume DIDIER on 04/02/2023.
//

#include "csapp.h"
#include "csapp_private.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/*
double NAN = 0.0/0.0;
double POS_INF = 1.0 /0.0;
double NEG_INF = -1.0/0.0;
*/

#define NUM_TEST 10
static double tests[NUM_TEST] = {0.0, 1.0, 10., 0.1, 0.0,
                                 0.0, 0.0, 0.0, 0.0, 0.0};

static double u64tod(uint64_t u) {
    union {
        double d;
        uint64_t u;
    } conv;
    conv.u = u;
    return conv.d;
}

static double round_trip(decoded_float_t decoded) {
    if (decoded.exponent < 0) {
        return pow(-1, decoded.sign) * pow(2, decoded.exponent + 2) *
               (double)decoded.mantissa * pow(2, -2);
    } else {
        return pow(-1, decoded.sign) * pow(2, decoded.exponent) *
               (double)decoded.mantissa;
    }
}

static void print_leading_zeros(uint64_t n) {
    sio_printf("%llx : %d leading zeros\n", n, uint64_leading_zeros(n));
}

int main(void) {
    sio_printf("log2(%d) = %zd\n", 0, uint32_log2(0));
    for (int i = 0; i < 32; i++) {
        sio_printf("log2(%d) = %zd\n", 1 << i, uint32_log2(1 << i));
    }

    uint32_t v = 7;
    uint32_t minus_v = -v;
    sio_printf("v: %x, -v:%x\n", v, minus_v);
    sio_printf("%x -> %x\n", 7, keepHighestBit(7));

    sio_printf("log2(7) = %zd\n", uint32_log2(7));

    tests[4] = u64tod((uint64_t)0x1);
    tests[5] = u64tod(0x800fffffffffffffULL);
    tests[6] = u64tod(0x0010000000000000ULL);
    tests[7] = u64tod(0x424242ULL);
    tests[8] = u64tod(0x8000000000000000ULL);
    tests[9] = u64tod(0x0000000000000000ULL);

    for (size_t i = 0; i < NUM_TEST; i++) {
        decoded_float_t decoded;
        float_kind_t type = decode_double(tests[i], &decoded);
        sio_assert(type == FK_FINITE || type == FK_ZERO);
        // We probably want a round trip function to help with that. The 52
        // comes from the offsetting of exponents done in decode_double
        double round_tripped = round_trip(decoded);
        /* Suppress warning : the point is to check that we did get back to the
         * correct float, bit for bit) */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
        if (round_tripped == tests[i]) {
#pragma clang diagnostic pop
            printf("OK: %.40lg == %.40lg, sign: %x, exponent: %d, mantissa: "
                   "%llx\n",
                   round_tripped, tests[i], decoded.sign, decoded.exponent,
                   decoded.mantissa);
        } else {
            printf("BAD %.40lg == %.40lg, sign: %x, exponent: %d, mantissa: "
                   "%llx\n",
                   round_tripped, tests[i], decoded.sign, decoded.exponent,
                   decoded.mantissa);
        }
    }
    srand(42);
    for (size_t i = 0; i < 1 << 15; i++) {
        uint64_t random_u64 = (unsigned int)rand();
        random_u64 = (random_u64 << 32) + (unsigned int)rand();
        double random_float = u64tod(random_u64);
        decoded_float_t decoded;
        if (decode_double(random_float, &decoded) != FK_FINITE) {
            printf("Not Finite\n");
            i--;
            continue;
        }
        double round_tripped = round_trip(decoded);
        /* Suppress warning : the point is to check that we did get back to the
         * correct float, bit for bit) */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
        if (round_tripped == random_float) {
#pragma clang diagnostic pop
            // printf("OK: %.40lg == %.40lg, sign: %x, exponent: %d, mantissa:
            // %llx\n", round_trip, tests[i], decoded.sign, decoded.exponent,
            // decoded.mantissa);
        } else {
            printf("BAD %.40lg == %.40lg, sign: %x, exponent: %d, mantissa: "
                   "%llx\n",
                   round_tripped, random_float, decoded.sign, decoded.exponent,
                   decoded.mantissa);
        }
    }
    printf("OK\n");

    // check_POW10TO_N();

    // decoded_float_t decoded_float;
    // float_kind_t kind = decode_double(INFINITY, &decoded_float);
    // printf("Decoded infinity %f: kind: %d, sign: %x, exponent: %d, mantissa:
    // %llx\n", INFINITY, kind, decoded_float.sign, decoded_float.exponent,
    // decoded_float.mantissa);

    sio_printf("%*f\n", 15, 0.0);
    printf("%*f\n", 15, 0.0);
    sio_printf("%*f\n", 15, -0.0);
    printf("%*f\n", 15, -0.0);
    sio_printf("%*f\n", 15, (double)+INFINITY);
    printf("%*f\n", 15, (double)+INFINITY);
    sio_printf("%*f\n", 15, (double)-INFINITY);
    printf("%*f\n", 15, (double)-INFINITY);
    sio_printf("%*f\n", 15, (double)NAN);
    printf("%*f\n", 15, (double)NAN);
    sio_printf("%*f\n", 15, u64tod((uint64_t)0x1));
    printf("%*f\n", 15, u64tod((uint64_t)0x1));
    sio_printf("%*f\n", 15, 1.0);
    printf("%*f\n", 15, 1.0);
    sio_printf("%*f\n", 15, 0.01);
    printf("%*f\n", 15, 0.01);
    sio_printf("%*f\n", 15, 10.0);
    printf("%*f\n", 15, 10.0);
    sio_printf("%*f\n", 15, 1234.5);
    printf("%*f\n", 15, 1234.5);
    sio_printf("%*f\n", 15, 32.0);
    printf("%*f\n", 15, 32.0);

    print_leading_zeros(0);
    print_leading_zeros(1);
    print_leading_zeros(3);
    print_leading_zeros(4);
    print_leading_zeros(0x00011100011100);
    print_leading_zeros(0xFFFFFFFFFFFFFFFF);

    return 0;
}
