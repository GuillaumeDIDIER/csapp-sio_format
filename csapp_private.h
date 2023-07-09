#ifndef CSAPP_PRIVATE_H
#define CSAPP_PRIVATE_H

#include <stdbool.h>
#include <stdint.h>

// This float is (-1)^sign * 2^exponent * mantissa (with mantissa an integer)
// minus and plus define the interval such that any number
// from -(1)^sign * (mant - minus) * 2^(exponent) to -(1)^sign * (mant + plus) *
// 2^(exponent) plus is thus always away from zero, and minus toward zero.
// rounds to this number
// The range is inclusive if inclusive is true.
typedef struct decoded_float {
    uint64_t mantissa;
    uint64_t minus;
    uint64_t plus;
    bool sign;
    bool inclusive;
    int16_t exponent;
} decoded_float_t;

typedef enum float_kind {
    FK_FINITE,
    FK_ZERO,
    FK_INFINITY,
    FK_NAN,
} float_kind_t;

size_t uint32_log2(uint32_t v);
uint32_t keepHighestBit(uint32_t n);

float_kind_t decode_double(double d, decoded_float_t *decoded);

#ifdef DEBUG
void check_POW10TO_N();
#endif // DEBUG

unsigned int uint64_leading_zeros(uint64_t n);

#endif // CSAPP_PRIVATE_H
