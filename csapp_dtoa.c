#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "csapp.h"
#include "csapp_dtoa.h"
#include "csapp_private.h"

/*Double is 1 sign bit, 11 exponent bits and 52 mantissa bits*/
float_kind_t decode_double(double d, decoded_float_t *decoded) {
    sio_assert(decoded != NULL);

    uint64_t bits;
    {
        union {
            double d;
            uint64_t u64;
        } conv;
        conv.d = d;
        bits = conv.u64;
    }
    // Now we have the bits.

    decoded->sign = bits >> 63;
    int16_t E = (bits >> 52) & 0x7ff; /*11 bits*/
    uint64_t M = bits & (((uint64_t)1 << 52) - 1);
    bool even = ((M & 1) == 0);

    float_kind_t ret = FK_FINITE;

    if (E == 0) {     // Zero or denormalized
        if (M == 0) { // Zero
            ret = FK_ZERO;
            decoded->mantissa = 0;
            decoded->exponent = 0;
            decoded->plus = 0;
            decoded->minus = 0;
            decoded->inclusive = even;
        } else { // Denormalized
            decoded->exponent = -(1023 + 52);
            decoded->mantissa = M << 1;
            decoded->plus = 1;
            decoded->minus = 1;
            decoded->inclusive = even;
        }
    } else if (E == 0x7ff) { // Inf or Nan
        // Sentinel values.
        decoded->exponent = -1;
        decoded->mantissa = ~((uint64_t)0);
        decoded->plus = ~((uint64_t)0);
        decoded->minus = ~((uint64_t)0);
        decoded->inclusive = false;
        if (M == 0) {
            ret = FK_INFINITY;
        } else {
            ret = FK_NAN;
        }
    } else { // Normal float
        decoded->exponent = E - (1023 /* bias */ + 52 /* matissa shift */);
        decoded->mantissa = M | ((uint64_t)1 << 52); // Add the implicit 1.
        // Now we need to deal with plus and minus, which may depend one whether
        // we are near the limit of an exponent :/
        if (M == 0) { // Smallest possible number with this exponent, the lower
                      // bound is half the usual.
            decoded->exponent -= 2;
            decoded->mantissa <<= 2;
            decoded->plus = 2;
            decoded->minus = 1;
            decoded->inclusive = even;
        } else {
            decoded->exponent--;
            decoded->mantissa <<= 1;
            decoded->plus = 1;
            decoded->minus = 1;
            decoded->inclusive = even;
        }
    }
    return ret;
}

/*
 * TODO: This code needs reviewing, these kind of operation are kind of
 * expensive.
 */
static bool carrying_add(uint32_t *a, uint32_t b, bool carry) {
    uint32_t tmp = *a;
    *a += b;
    bool c = (*a < tmp); // overflow occurred if a went down.

    tmp = *a;
    *a += (uint32_t)carry;

    c = c || (*a < tmp);
    return c;
}

static uint32_t carrying_mul(uint32_t *a, uint32_t b, uint32_t carry) {
    uint64_t res = (uint64_t)(*a) * (uint64_t)b + (uint64_t)carry;
    *a = (uint32_t)res;
    return (uint32_t)(res >> (CHAR_BIT * sizeof(uint32_t)));
}

// returns h such as h << 32 + l = f1 + f2 + t2 + carry
static uint32_t full_mul_add(uint32_t *l, uint32_t f1, uint32_t f2, uint32_t t2,
                             uint32_t carry) {
    uint64_t r = (uint64_t)f1 * (uint64_t)f2 + (uint64_t)t2 + (uint64_t)carry;
    *l = (uint32_t)r;
    return (uint32_t)(r >> (CHAR_BIT * sizeof(uint32_t)));
}

static void full_div_rem(uint32_t self, uint32_t d, uint32_t borrow,
                         uint32_t *q, uint32_t *r) {
#ifdef DEBUG
    sio_assert(borrow < d);
// Otherwise the division of the previous digit went wrong.
#endif // DEBUG
    uint64_t a = (((uint64_t)borrow) << (CHAR_BIT * sizeof(uint32_t))) | self;
    uint64_t b = (uint64_t)d;
    *q = (uint32_t)(a / b);
    *r = (uint32_t)(a % b);
}

static size_t maxz(size_t a, size_t b) {
    if (a > b) {
        return a;
    } else {
        return b;
    }
}

#define BIG_NUM_SIZE 40
#define DIGIT_BITS 32

// const SMALL_POW5: [(u64, usize); 3] = [(125, 3), (15625, 6), (1_220_703_125,
// 13)];
static const struct {
    size_t exp;
    uint32_t value;
} small_pow_5 = {13, 1220703125};

/* Inspire by rust num BigNum32x40
 */
typedef struct {
    size_t size; // index of the first unused digit, aka one plus the index of
                 // the largest digit used base[size+i] is 0 for all i such that
                 // size+i < BIG_NUM_SIZE
    uint32_t base[BIG_NUM_SIZE]; // the number is base[0] + 2^32 base[1] + ...
} bignum32x40_t;

/*
 * Undefined behaviour if big is NULL;
 */
static void bignum32x40_from_uint32(bignum32x40_t *big, uint32_t small) {
    sio_assert(big != NULL);
    big->size = 1;
    memset(big->base, 0, BIG_NUM_SIZE * sizeof(uint32_t));
    big->base[0] = small;
}

static void bignum32x40_from_uint64(bignum32x40_t *big, uint64_t v) {
    sio_assert(big != NULL);
    big->size = 2;
    memset(big->base, 0, BIG_NUM_SIZE * sizeof(uint32_t));
    big->base[0] = (uint32_t)v;
    big->base[1] = (uint32_t)(v >> DIGIT_BITS);
}

static void bignum32x40_clone(const bignum32x40_t *self, bignum32x40_t *dest) {
    sio_assert(self != dest);
    sio_assert(self != NULL);
    sio_assert(dest != NULL);
    dest->size = self->size;
    memcpy(dest->base, self->base, BIG_NUM_SIZE * sizeof(uint32_t));
}

// Safety : digit_size must reflect the size of the array pointed to by digits
static bignum32x40_t *bignum32x40_from_digits(bignum32x40_t *big,
                                              size_t digit_size,
                                              uint32_t *digits) {
    sio_assert(big != NULL);
    sio_assert(digits != NULL);
    if (digit_size > BIG_NUM_SIZE) {
        return NULL;
    }
    big->size = 0;
    memset(big->base, 0, BIG_NUM_SIZE * sizeof(uint32_t));
    for (size_t i = 0; i < digit_size; i++) {
        big->base[i] = digits[i];
        if (big->base[i] != 0) {
            big->size = i + 1;
        }
    }
    return big;
}

static uint8_t bignum32x40_get_bit(const bignum32x40_t *self, size_t i) {
    sio_assert(self != NULL);
    // size_t digitbits = CHAR_BIT * sizeof(uint32_t);
    size_t d = i / DIGIT_BITS;
    size_t b = i % DIGIT_BITS;
    return (uint8_t)((self->base[d] >> b) & 0x1);
}

static bool _bignum32x40_is_zero_full(const bignum32x40_t *self) {
    sio_assert(self != NULL);
    for (size_t i = 0; i < BIG_NUM_SIZE; i++) {
        if (self->base[i] != 0) {
            return false;
        }
    }

    return true; // unimplemented
}

static bool bignum32x40_is_zero(const bignum32x40_t *self) {
    sio_assert(self != NULL);
    for (size_t i = 0; i < self->size; i++) {
        if (self->base[i] != 0) {
            return false;
        }
    }
    sio_assert(_bignum32x40_is_zero_full(self));
    return true;
}
#if 0
#define LT(n) n, n, n, n, n, n, n, n, n, n, n, n, n, n, n, n
static const char LogTable256[256] = {
    -1,    0,     1,     1,     2,     2,     2,     2,     3,     3,     3,
    3,     3,     3,     3,     3,     LT(4), LT(5), LT(5), LT(6), LT(6), LT(6),
    LT(6), LT(7), LT(7), LT(7), LT(7), LT(7), LT(7), LT(7), LT(7)};


/* Inspired by https://stackoverflow.com/questions/11376288/fast-computing-of-log2-for-64-bit-integers
 */
static size_t uint32_log2(uint32_t n) {
    uint32_t t = n >> 16;
    if (t != 0) {
        uint32_t tt = t >> 8;
        if (tt != 0) {
            return 24 + LogTable256[tt];
        } else {
            return 16 + LogTable256[t];
        }
    } else {
        uint32_t tt = t >> 8;
        if (tt != 0) {
            return 8 + LogTable256[tt];
        } else {
            return LogTable256[t];
        }
    }
}

// From Linux kernel headers.
// This function may allow a faster implementation of log2
/**
 * fls - find last (most-significant) bit set
 * @x: the word to search
 *
 * This is defined the same way as ffs.
 * Note fls(0) = 0, fls(1) = 1, fls(0x80000000) = 32.
 */
static inline int fls(int x) {
    int r = 32;

    if (!x) {
        return 0;
    }
    if (!(x & 0xffff0000u)) {
        x <<= 16;
        r -= 16;
    }
    if (!(x & 0xff000000u)) {
        x <<= 8;
        r -= 8;
    }
    if (!(x & 0xf0000000u)) {
        x <<= 4;
        r -= 4;
    }
    if (!(x & 0xc0000000u)) {
        x <<= 2;
        r -= 2;
    }
    if (!(x & 0x80000000u)) {
        x <<= 1;
        r -= 1;
        }
    return r;
}

#else

/* NOTE : We should really add test for those utility functions
 */

// From https://en.wikipedia.org/wiki/De_Bruijn_sequence
uint32_t keepHighestBit(uint32_t n) {
    n |= (n >> 1);
    n |= (n >> 2);
    n |= (n >> 4);
    n |= (n >> 8);
    n |= (n >> 16);
    return n - (n >> 1);
}

size_t uint32_log2(uint32_t v) {
    static const uint8_t BitPositionLookup[32] = {
        // hash table
        0,  1,  16, 2,  29, 17, 3,  22, 30, 20, 18, 11, 13, 4, 7,  23,
        31, 15, 28, 21, 19, 10, 12, 6,  14, 27, 9,  5,  26, 8, 25, 24,
    };
    return BitPositionLookup[(keepHighestBit(v) * 0x06EB14F9U) >> 27];
}

#endif // 0

// if gcc / clang, we might use int __builtin_clz (unsigned int x) instead of
// log2

static size_t bignum32x40_bit_length(const bignum32x40_t *self) {
    sio_assert(self != NULL);
    for (size_t i = 1; i <= self->size; i++) {
        if (self->base[self->size - i] != 0) {
            return self->size - i + uint32_log2(self->base[self->size - i]) + 1;
        }
    }
    // the number is zero
    return 0;
}

// Note modifies self and returns pointer to self.
static bignum32x40_t *bignum32x40_add(bignum32x40_t *self,
                                      const bignum32x40_t *other) {
    sio_assert(self != NULL);
    sio_assert(other != NULL);
    size_t sz = maxz(self->size, other->size);
    bool carry = false;
    for (size_t i = 0; i < sz; i++) {
        uint32_t *a = &self->base[i];
        uint32_t b = other->base[i];
        carry = carrying_add(a, b, carry);
    }
    if (carry) {
        if (sz == BIG_NUM_SIZE) {
            __sio_assert_fail("Big number overflowed", __FILE__, __LINE__,
                              __func__);
        }
        self->base[sz] = 1;
        sz += 1;
    }
    self->size = sz;

    return self;
}

static bignum32x40_t *bignum32x40_add_small(bignum32x40_t *self,
                                            uint32_t small) {
    sio_assert(self != NULL);
    bool carry = carrying_add(&self->base[0], small, false);
    size_t i = 1;
    while (carry) {
        if (i == BIG_NUM_SIZE) {
            __sio_assert_fail("Big number overflowed", __FILE__, __LINE__,
                              __func__);
        }
        carry = carrying_add(&self->base[i], 0, carry);
        i += 1;
    }
    if (i > self->size) {
        self->size = i;
    }
    sio_assert(self->size <= BIG_NUM_SIZE);
    return self;
}

// Note : other must be less or equal than self.
static bignum32x40_t *bignum32x40_sub(bignum32x40_t *self,
                                      const bignum32x40_t *other) {
    sio_assert(self != NULL);
    sio_assert(other != NULL);
    size_t sz = maxz(self->size, other->size);
    bool noborrow = true;
    for (size_t i = 0; i < sz; i++) {
        uint32_t *a = &self->base[i];
        uint32_t b = other->base[i];
        noborrow = carrying_add(a, ~b, noborrow);
    }
    sio_assert(noborrow);
    self->size = sz;
    return self;
}

/*
static bignum32x40_t* bignum32x40_mul(bignum32x40_t* self, const bignum32x40_t*
other) { sio_assert(false); return NULL; // unimplemented
}
*/

// This seems wrong.
static bignum32x40_t *bignum32x40_mul_small(bignum32x40_t *self,
                                            uint32_t small) {
    sio_assert(self != NULL);
    size_t sz = self->size;
    uint32_t carry = 0;
    for (size_t i = 0; i < sz; i++) {
        uint32_t *a = &self->base[i];
        carry = carrying_mul(a, small, carry);
    }
    if (carry > 0) {
        if (sz == BIG_NUM_SIZE) {
            __sio_assert_fail("Big number overflowed", __FILE__, __LINE__,
                              __func__);
        }
        self->base[sz] = carry;
        sz += 1;
    }
    self->size = sz;
    return self;
}

static bignum32x40_t *bignum32x40_mul_pow2(bignum32x40_t *self, size_t bits) {
    sio_assert(self != NULL);
    size_t digits = bits / DIGIT_BITS;
    bits = bits % DIGIT_BITS;
    sio_assert(digits < BIG_NUM_SIZE);
#ifdef DEBUG
    for (size_t i = BIG_NUM_SIZE - n; i < BIG_NUM_SIZE; i++) {
        sio_assert(self->base[i] == 0);
    }
#endif // DEBUG

    sio_assert(bits == 0 ||
               self->base[BIG_NUM_SIZE - digits - 1] >> (BIG_NUM_SIZE - bits) ==
                   0);
    sio_assert(self->size + digits <= BIG_NUM_SIZE);
    for (size_t i = 1; i <= self->size; i++) {
        size_t index = self->size - i; // reverse iteration.
        self->base[index + digits] = self->base[index];
    }
    for (size_t i = 0; i < digits; i++) {
        self->base[i] = 0;
    }
    size_t sz = self->size + digits;
    if (bits > 0) {
        size_t last = sz;
        uint32_t overflow = self->base[last - 1] >> (DIGIT_BITS - bits);
        if (overflow > 0) {
            self->base[last] = overflow;
            sz += 1;
        }
        for (size_t i = last - 1; i > digits; i--) {
            self->base[i] = (self->base[i] << bits) |
                            (self->base[i - 1] >> (DIGIT_BITS - bits));
        }
        self->base[digits] <<= bits;
    }
    self->size = sz;
    return self;
}

static bignum32x40_t *bignum32x40_mul_pow5(bignum32x40_t *self, size_t e) {
    sio_assert(self != NULL);
    while (e >= small_pow_5.exp) {
        bignum32x40_mul_small(self, small_pow_5.value);
        e -= small_pow_5.exp;
    }

    // Note GD, should this be optimized with fast exponentiation ?
    // Perhaps add an if (e > 0) around this last logic.

    uint32_t rest_power = 1;
    for (size_t i = 0; i < e; i++) {
        rest_power *= 5;
    }

    return bignum32x40_mul_small(self, rest_power); // unimplemented
}

// This is rust's mul_inner, which is optimized for a->size <= b->size.
static size_t bignum32x40_mul_helper(uint32_t *ret, const uint32_t *a,
                                     size_t sza, const uint32_t *b,
                                     size_t szb) {
    size_t retsz = 0;
    for (size_t i = 0; i < sza; i++) {
        if (a[i] == 0) {
            continue;
        }
        size_t sz = szb;
        size_t j = 0;
        uint32_t carry = 0;
        for (; j < sz; j++) {
            uint32_t v;
            if (i + j >= BIG_NUM_SIZE) {
                __sio_assert_fail("Big number overflowed", __FILE__, __LINE__,
                                  __func__);
            }
            uint32_t c = full_mul_add(&v, a[i], b[j], ret[i + j], carry);
            ret[i + j] = v;
            carry = c;
        }
        if (carry > 0) {
            if (i + sz >= BIG_NUM_SIZE) {
                __sio_assert_fail("Big number overflowed", __FILE__, __LINE__,
                                  __func__);
            }
            ret[i + sz] = carry;
            sz++;
        }
        if (retsz < i + sz) {
            retsz = i + sz;
        }
    }
    return retsz;
}

// This needs to allocate a third array.
static bignum32x40_t *bignum32x40_mul_digits(bignum32x40_t *self,
                                             const uint32_t *digits, size_t n) {
    sio_assert(self != NULL);
    sio_assert(digits != NULL);
    uint32_t ret[BIG_NUM_SIZE] = {0};
    size_t retsz =
        (self->size < n)
            ? bignum32x40_mul_helper(&ret[0], self->base, self->size, digits, n)
            : bignum32x40_mul_helper(&ret[0], digits, n, self->base,
                                     self->size);
    memcpy(&self->base[0], &ret[0], maxz(retsz, self->size));
    self->size = retsz;
    return self;
}

static uint32_t bignum32x40_div_rem_small(bignum32x40_t *self, uint32_t small) {
    sio_assert(self != NULL);

    size_t sz = self->size;
    uint32_t borrow = 0;
    for (size_t i = 1; i <= sz; i++) {
        size_t index = sz - i;
        uint32_t q = 0, r = 0;
        full_div_rem(self->base[index], small, borrow, &q, &r);
        self->base[index] = q;
        borrow = r;
    }
    return borrow;
}

/* Not needed in dragon apparently
static void bignum32x40_div_rem(bignum32x40_t *self, const bignum32x40_t *d,
                                bignum32x40_t *q, bignum32x40_t *r) {
    sio_assert(self != NULL);
    sio_assert(d != NULL);
    sio_assert(q != NULL);
    sio_assert(r != NULL);

    sio_assert(false); // unimplemented
}
*/

/*
 * NOTE : This is NOT a constant time implementation.
 */
static bool bignum32x40_eq(const bignum32x40_t *self,
                           const bignum32x40_t *other) {
    sio_assert(self != NULL);
    sio_assert(other != NULL);
    size_t max_size = maxz(self->size, other->size);
    for (size_t i = 0; i < max_size; i++) {
        if (self->base[i] != other->base[i]) {
            return false;
        }
    }
    return true;
}

static int bignum32x40_cmp(const bignum32x40_t *self,
                           const bignum32x40_t *other) {
    sio_assert(self != NULL);
    sio_assert(other != NULL);
    size_t max_size = maxz(self->size, other->size);
    for (size_t i = 1; i <= max_size; i++) {
        uint32_t a = self->base[max_size - i];
        uint32_t b = other->base[max_size - i];
        if (a < b) {
            return -1;
        }
        if (a > b) {
            return 1;
        }
    }
    // All the bytes are equal
    return 0;
}

// Use sio_format parameters, TODO, check error handling.
static ssize_t bignum32x40_format(const bignum32x40_t *self,
                                  sio_output_function output,
                                  void *output_state) {
    ssize_t ret = 0;
    for (size_t i = 1; i <= self->size; i++) {
        ssize_t r =
            sio_format(output, output_state, "%x", self->base[self->size - i]);
        if (r < 0) {
            return r;
        }
        ret += r;
    }
    return ret;
}

/* ************************************************************************** */
/* Dragon Algorithm Implementation                                            */
/* ************************************************************************** */

#define SMALL_POW10_MAX 9

/* The successive powers of 10 that fit in a digit */
static const uint32_t POW10[SMALL_POW10_MAX + 1] = {
    1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000};

/* 2 times the above sequence */
static const uint32_t TWOPOW10[SMALL_POW10_MAX + 1] = {
    2, 20, 200, 2000, 20000, 200000, 2000000, 20000000, 200000000, 2000000000};

// If we had C11 we would statically assert that both arrays have the same size.

/* Precalculated arrays of digits for 10^(2^n) */
#define DIGIT_SLICE(name, N, ...)                                              \
    static const struct {                                                      \
        size_t size;                                                           \
        uint32_t digits[N];                                                    \
    } name = {N, __VA_ARGS__}

DIGIT_SLICE(POW10TO16, 2, {0x6fc10000, 0x2386f2});
DIGIT_SLICE(POW10TO32, 4, {0, 0x85acef81, 0x2d6d415b, 0x4ee});
DIGIT_SLICE(POW10TO64, 7,
            {0, 0, 0xbf6a1f01, 0x6e38ed64, 0xdaa797ed, 0xe93ff9f4, 0x184f03});
DIGIT_SLICE(POW10TO128, 14,
            {0, 0, 0, 0, 0x2e953e01, 0x3df9909, 0xf1538fd, 0x2374e42f,
             0xd3cff5ec, 0xc404dc08, 0xbccdb0da, 0xa6337f19, 0xe91f2603,
             0x24e});
DIGIT_SLICE(POW10TO256, 27,
            {0,          0,          0,          0,          0,
             0,          0,          0,          0x982e7c01, 0xbed3875b,
             0xd8d99f72, 0x12152f87, 0x6bde50c6, 0xcf4a6e70, 0xd595d80f,
             0x26b2716e, 0xadc666b0, 0x1d153624, 0x3c42d35a, 0x63ff540e,
             0xcc5573c0, 0x65f9ef17, 0x55bc28f2, 0x80dcc7f7, 0xf46eeddc,
             0x5fdcefce, 0x553f7});

#ifdef DEBUG

// Used to check that the macro above worked as expected
void check_POW10TO_N() {
    sio_assert(POW10TO16.size == 2);
    sio_assert(POW10TO16.digits[0] == 0x6fc10000);
    sio_assert(POW10TO16.digits[1] == 0x2386f2);

    sio_assert(POW10TO32.size == 4);
    sio_assert(POW10TO32.digits[0] == 0);
    sio_assert(POW10TO32.digits[1] == 0x85acef81);
    sio_assert(POW10TO32.digits[2] == 0x2d6d415b);
    sio_assert(POW10TO32.digits[3] == 0x4ee);
}
#endif // DEBUG

/* Multiply self by 10^(n)
 * Only works up to n = 511 */
static bignum32x40_t *bignum32x40_mul_pow10(bignum32x40_t *self, size_t n) {
    sio_assert(self != NULL);
    sio_assert(n < 512);
    if ((n & 7) != 0) {
        bignum32x40_mul_small(self, POW10[n & 7]);
    }
    if ((n & 8) != 0) {
        bignum32x40_mul_small(self, POW10[8]);
    }
    if ((n & 16) != 0) {
        bignum32x40_mul_digits(self, &POW10TO16.digits[0], POW10TO16.size);
    }
    if ((n & 32) != 0) {
        bignum32x40_mul_digits(self, &POW10TO32.digits[0], POW10TO32.size);
    }
    if ((n & 64) != 0) {
        bignum32x40_mul_digits(self, &POW10TO64.digits[0], POW10TO64.size);
    }
    if ((n & 128) != 0) {
        bignum32x40_mul_digits(self, &POW10TO128.digits[0], POW10TO128.size);
    }
    if ((n & 256) != 0) {
        bignum32x40_mul_digits(self, &POW10TO256.digits[0], POW10TO256.size);
    }
    return self;
}

static bignum32x40_t *bignum32x40_div_2pow10(bignum32x40_t *self, size_t n) {
    sio_assert(self != NULL);
    while (n > SMALL_POW10_MAX) {
        bignum32x40_div_rem_small(self, POW10[SMALL_POW10_MAX]);
        n -= SMALL_POW10_MAX;
    }
    bignum32x40_div_rem_small(self, TWOPOW10[n]);
    return self;
}

static unsigned int uint32_leading_zeros(uint32_t n) {
    unsigned int ret = 0;
    if (n & 0xFFFF0000) {
        n >>= 16;
    } else {
        ret += 16;
    }
    if (n & 0xFF00) {
        n >>= 8;
    } else {
        ret += 8;
    }

    if (n & 0xF0) {
        n >>= 4;
    } else {
        ret += 4;
    }

    if (n & 0xC) {
        n >>= 2;
    } else {
        ret += 2;
    }

    if (n & 0x2) {
        n >>= 1;
    } else {
        ++ret;
    }

    if (n == 0) {
        ++ret;
    }
    return ret;
}

unsigned int uint64_leading_zeros(uint64_t n) {
    if (n & 0xFFFFFFFF00000000) {
        return uint32_leading_zeros((uint32_t)(n >> 32));
    } else {
        return uint32_leading_zeros((uint32_t)n) + 32;
    }
}

/* The exponent estimator.

 Finds `k_0` such that `10^(k_0-1) < mant * 2^exp <= 10^(k_0+1)`.

 This is used to approximate `k = ceil(log_10 (mant * 2^exp))`;
 the true `k` is either `k_0` or `k_0+1`. */
static int16_t estimate_scaling_factor(uint64_t mantissa, int16_t exp) {
    // 2^(nbits-1) < mant <= 2^nbits if mant > 0
    int64_t nbits = 64 - (int64_t)(uint64_leading_zeros(mantissa - 1));

    // 1292913986 = floor(2^32 * log_10 2)
    // therefore this always underestimates (or is exact), but not much.
    return (int16_t)((nbits + (int64_t)exp) * 1292913986 >> 32);
}

/* Rounds up a decimal digit
 * If the length change, it returns the additional least significant digit
 */
static char round_up(char *digit_buffer, int len) {
    int i = len - 1;
    while (i >= 0 && digit_buffer[i] == '9') {
        i--;
    }
    // i points to a non 9 digit, or is negative
    if (i < 0) {
        if (len == 0) {
            return '1'; // The empty buffer rounds up to 1. (Edge case to be
                        // understood)
        } else {
            // 999..999 rounds to 1000..000 with an increased exponent
            digit_buffer[0] = '1';
            for (int j = 0; j < len; j++) {
                digit_buffer[j] = '0';
            }
            return '0';
        }
    } else {
        // digit_buffer[i+1 to digit_buffer[n] are all nines.
        digit_buffer[i]++;
        for (int j = i + 1; j < len; j++) {
            digit_buffer[j] = '0';
        }
        return 0; // Not a digit !
    }
}

/* NOTE C is dumb, and has no format specifier for float that results in an
 * optimal float printing, aka output the minimum number of digit required for
 * successful round-trip and only the minimum.
 * Consequently, we do not need the format_short version of dragon.
 * Only the format_exact mode is necessary for sio_printf
 *
 * We will eventually, however, add the "short" version here and export it,
 * so that people can use it is so they wish,
 * provided we figure out the requisite constraints on the buffer
 */

// Sizing of the buffer to be determined.
// For short mode it will be given by `ceil(# bits in mantissa * log_10 2 + 1)`.
// Which for double gives 17

static size_t sio_double_to_digits_short(decoded_float_t *d, char *digit_buffer,
                                         size_t buffer_size,
                                         int16_t *exponent) {
    sio_assert(d->mantissa > 0);
    sio_assert(d->minus > 0);
    sio_assert(d->plus > 0);
    sio_assert(d->mantissa + d->plus > d->mantissa);  // check for overflow
    sio_assert(d->mantissa - d->minus < d->mantissa); // check for underflow
    sio_assert(false);                                // unimplemented
    return 0;
}

static size_t sio_double_to_digits_exact(decoded_float_t *d, char *digit_buffer,
                                         size_t buffer_size, int16_t *exponent,
                                         int16_t limit) {
    sio_assert(buffer_size < INT_MAX);
    sio_assert(d->mantissa > 0); // plus or minus are unneeded here

    int16_t k = estimate_scaling_factor(
        d->mantissa, d->exponent); // TODO estimate scaling factor

    // The real value v = mant / scale
    bignum32x40_t mant, scale;
    bignum32x40_from_uint64(&mant, d->mantissa);
    bignum32x40_from_uint32(&scale, 1);
    // depending on the sign of the exponent multiply the mantissa or the scale
    if (d->exponent < 0) {
        bignum32x40_mul_pow2(&scale, (size_t)(-d->exponent));
    } else {
        bignum32x40_mul_pow2(&mant, (size_t)d->exponent);
    }

    // Now let's bring v between 0.1 and 10 by dividing by 10^k
    if (k >= 0) {
        bignum32x40_mul_pow10(&scale, (size_t)k);
    } else {
        bignum32x40_mul_pow10(&mant, (size_t)(-k));
    }

    { // fixup is not needed later
        bignum32x40_t fixup;
        bignum32x40_clone(&scale, &fixup);
        bignum32x40_div_2pow10(&fixup, buffer_size);
        bignum32x40_add(&fixup, &mant);
        if (bignum32x40_cmp(&fixup, &scale) >= 0) {
            k++;
        } else {
            bignum32x40_mul_small(&mant, 10);
        }
    }

    // Adjust the buffer size
    size_t len;
    if (k < limit) {
        len = 0;
    } else if ((size_t)((ssize_t)k - (ssize_t)limit) < buffer_size) {
        len = (size_t)((ssize_t)k - (ssize_t)limit);
    } else {
        len = buffer_size;
    }

    if (len > 0) {
        bignum32x40_t scale2, scale4, scale8;
        bignum32x40_clone(&scale, &scale2);
        bignum32x40_mul_pow2(&scale2, 1);
        bignum32x40_clone(&scale, &scale4);
        bignum32x40_mul_pow2(&scale4, 2);
        bignum32x40_clone(&scale, &scale8);
        bignum32x40_mul_pow2(&scale8, 3);
        // Alternate method of chaining muls is likely less efficient on a
        // modern O3 architecture.

        for (size_t i = 0; i < len; i++) {
            if (bignum32x40_is_zero(&mant)) {
                for (size_t j = i; j < len; j++) {
                    digit_buffer[j] = '0';
                }
                *exponent = k;
                return len;
            }
            char digit = '0';
            if (bignum32x40_cmp(&mant, &scale8) >= 0) {
                bignum32x40_sub(&mant, &scale8);
                digit += 8;
            }
            if (bignum32x40_cmp(&mant, &scale4) >= 0) {
                bignum32x40_sub(&mant, &scale4);
                digit += 4;
            }
            if (bignum32x40_cmp(&mant, &scale2) >= 0) {
                bignum32x40_sub(&mant, &scale2);
                digit += 2;
            }
            if (bignum32x40_cmp(&mant, &scale) >= 0) {
                bignum32x40_sub(&mant, &scale);
                digit += 1;
            }
#ifdef DEBUG
            sio_assert(bignum32x40_cmp(&mant, &scale) < 0);
            sio_assert(digit <= '9');
#endif // DEBUG
            digit_buffer[i] = digit;
            bignum32x40_mul_small(&mant, 10);
        }
    }

    bignum32x40_mul_small(&scale, 5);
    int order = bignum32x40_cmp(&mant, &scale);
    if (order > 0 ||
        (order == 0 && len > 0 && ((digit_buffer[len - 1] & 1) == 1))) {
        // We need to round UP
        char digit = round_up(
            digit_buffer, (int)len); // Safe as we know the buffer size is safe
        if (digit != 0) {
            k++;
            if (k > limit && len < buffer_size) {
                digit_buffer[len] = digit;
                len++;
            }
        }
    }
    *exponent = k;
    return len;
}

ssize_t sio_format_double_shortest(sio_output_function output,
                                   void *output_state, double d,
                                   dtoa_flags_t flags, ssize_t padding) {
    sio_assert(false); // unimplemented
    return 0;
}

/* TODO: Sign flags are unsupported for now */
ssize_t sio_format_double_exact(sio_output_function output, void *output_state,
                                double d, dtoa_flags_t flags,
                                ssize_t padding, int precision) {
    // First allocate an appropriately sized buffer ?
    // Rust uses 1024, to be determined if this is acceptable here.

    decoded_float_t decoded;
    float_kind_t float_kind = decode_double(d, &decoded);

    if (flags != FORMAT_f) {
        // Unsupported
        sio_assert(false);
    }

    size_t left_padding = 0;
    size_t right_padding = 0;

    if (padding > 0) {
        left_padding = (size_t) padding;
    } else {
        right_padding = (size_t )  (-padding);
    }

    switch (float_kind) {
    case FK_FINITE: {
        char buffer[DTOA_EXACT_BUFFER_SIZE] = {0};
        char *data = &buffer[0];
        int16_t exponent;
        if (decoded.sign) {
            *data = '-';
            data++;
        }
        int16_t limit;
        if (precision < -((int)INT16_MIN)) {
            limit = (int16_t)-precision;
        } else {
            limit = INT16_MIN;
        }
        size_t digits = sio_double_to_digits_exact(
            &decoded, data, DTOA_EXACT_BUFFER_SIZE, &exponent, limit);
        if (exponent <= limit) {
            // this is too small and renders as 0
            sio_assert(digits == 0);
            *data = '0';
            data++;
            size_t length;
            if (precision > 0) {
                *data = '.';
                data++;
                length = strlen(buffer) + (size_t)precision;
            } else {
                length = strlen(buffer);
            }
            size_t left_padding_count = 0;
            size_t right_padding_count = 0;
            if (left_padding > length) {
                left_padding_count = left_padding - length;
            }
            if (right_padding > length) {
                right_padding_count = right_padding - length;
            }

            ssize_t res = output(output_state, ' ', left_padding_count, 0, buffer,
                                 strlen(buffer));
            if (res < 0) {
                return -1;
            }
            if (precision > 0) {
                ssize_t r = output(output_state, '0', (size_t)precision, 0, NULL,
                                   0);
                if (r < 0) {
                    return -1;
                }
                res += r;

            }
            ssize_t r = output(output_state, ' ', (size_t)right_padding_count, 0, NULL,
                       0);
            if (r < 0) {
                return -1;
            }
            res += r;
            return res;
        } else { // digit to decimal string.
                 // This may turn into a help if we implement exponential form.
            sio_assert(digits > 0); // There are digits to print
            sio_assert(*data > '0' && *data <= '9');
            if (exponent < 0) {
                // The decimal points comes first
                // 0. 000000 1234
                // Will output space padding and 0. first
                // then 0 padding and digits.
                // then if needed additional zeros

                size_t minus_exp = (size_t)(-(
                    ssize_t)exponent);     // We know th sign of exponent.
                sio_assert(precision > 0); // Otherwise we should not be here
                size_t length = decoded.sign + strlen("0.") + (size_t)precision;
                size_t left_padding_count = 0;
                size_t right_padding_count = 0;
                if (left_padding > length) {
                    left_padding_count = left_padding - length;
                }
                if (right_padding > length) {
                    right_padding_count = right_padding - length;
                }
                ssize_t res;
                if (decoded.sign) {
                    res = output(output_state, ' ', left_padding_count, 0, "-0.",
                                 strlen("-0.")); // TODO error handling.
                } else {
                    res = output(output_state, ' ', left_padding_count, 0, "0.",
                                 strlen("0.")); // TODO error handling.
                }
                if (res < 0) {
                    return -1;
                }
                ssize_t r = output(output_state, '0', minus_exp, 0, data,
                                   strlen(data)); // TODO error handling.
                if (r < 0) {
                    return -1;
                }
                res += r;
                if (precision > 0 &&
                    minus_exp + strlen(data) < (size_t)precision) {
                    r = output(output_state, '0',
                               (size_t)precision - (minus_exp + strlen(data)), 0,
                               NULL, 0); // TODO error handling.
                    if (r < 0) {
                        return -1;
                    }
                    res += r;
                }
                r = output(output_state, ' ', (size_t)right_padding_count, 0, NULL,
                                   0);
                if (r < 0) {
                    return -1;
                }
                res += r;
                return res;
            } else { // exponent >= 0
                size_t sz_exponent = (uint16_t)exponent;
                if (sz_exponent < digits) {
                    // The exponent is inside the digits 1234.5678
                    size_t length;
                    if (precision > 0) {
                        length =
                            decoded.sign + sz_exponent + (size_t)precision + 1;
                    } else {
                        length = decoded.sign + sz_exponent;
                    }
                    size_t left_padding_count = 0;
                    size_t right_padding_count = 0;
                    if (left_padding > length) {
                        left_padding_count = left_padding - length;
                    }
                    if (right_padding > length) {
                        right_padding_count = right_padding - length;
                    }

                    ssize_t res = output(output_state, ' ', left_padding_count, 0,
                                         buffer, sz_exponent + decoded.sign);
                    if (res < 0) {
                        return -1;
                    }
                    ssize_t r = output(output_state, '.', 1, 0, data + sz_exponent,
                                       digits - sz_exponent);
                    if (r < 0) {
                        return -1;
                    }
                    res += r;
                    if (precision >= 0 &&
                        (size_t)precision > digits - sz_exponent) {
                        r = output(output_state, '0',
                                   (size_t)precision - digits + sz_exponent, 0,
                                   NULL, 0);
                        if (r < 0) {
                            return -1;
                        }
                        res += r;
                    }
                    r = output(output_state, ' ', (size_t)right_padding_count, 0, NULL,
                               0);
                    if (r < 0) {
                        return -1;
                    }
                    res += r;
                    return res;
                } else {
                    // The point is after the number
                    size_t length;
                    if (precision > 0) {
                        length =
                            decoded.sign + sz_exponent + (size_t)precision + 1;
                    } else {
                        length = decoded.sign + sz_exponent;
                    }
                    size_t left_padding_count = 0;
                    size_t right_padding_count = 0;
                    if (left_padding > length) {
                        left_padding_count = left_padding - length;
                    }
                    if (right_padding > length) {
                        right_padding_count = right_padding - length;
                    }
                    ssize_t res = output(output_state, ' ', left_padding_count, 0,
                                         buffer, digits + decoded.sign);
                    if (res < 0) {
                        return -1;
                    }
                    if (precision > 0) {
                        ssize_t r = output(output_state, '0',
                                           sz_exponent - digits, 0, ".", 1);
                        if (r < 0) {
                            return -1;
                        }
                        res += r;
                        r = output(output_state, '0', (size_t)precision, 0, NULL,
                                   0);
                        if (r < 0) {
                            return -1;
                        }
                        res += r;
                    } else {
                        ssize_t r = output(output_state, '0',
                                           sz_exponent - digits, 0, NULL, 0);
                        if (r < 0) {
                            return -1;
                        }
                        res += r;
                    }
                    ssize_t r = output(output_state, ' ', (size_t)right_padding_count, 0, NULL,
                               0);
                    if (r < 0) {
                        return -1;
                    }
                    res += r;
                    return res;
                }
            }
        }
    }
    case FK_ZERO: {
        // This needs to change if we are to support exponential formats.
        char *data;
        if (decoded.sign) {
            data = "-0.";
        } else {
            data = "0.";
        }
        size_t length;
        if (precision > 0) {
            length = strlen(data) + (size_t)precision;
        } else {
            length = strlen(data) - 1;
        }
        size_t left_padding_count = 0;
        size_t right_padding_count = 0;
        if (left_padding > length) {
            left_padding_count = left_padding - length;
        }
        if (right_padding > length) {
            right_padding_count = right_padding - length;
        }

        ssize_t res = output(output_state, ' ', left_padding_count, 0, data,
                             strlen(data) - (precision <= 0));
        if (res < 0) {
            return -1;
        }
        if (precision > 0) {
            ssize_t r = output(output_state, '0', (size_t)precision, 0, NULL, 0);
            if (r < 0) {
                return -1;
            }
            res += r;
        }
        ssize_t r = output(output_state, ' ', (size_t)right_padding_count, 0, NULL,
                           0);
        if (r < 0) {
            return -1;
        }
        res += r;
        return res;
    }
    case FK_INFINITY: {
        char *data;
        if (decoded.sign) {
            data = "-inf";
        } else {
            data = "inf";
        }
        size_t left_padding_count = 0;
        size_t right_padding_count = 0;
        if (left_padding > (strlen(data))) {
            left_padding_count = left_padding - (strlen(data));
        }
        if (right_padding > (strlen(data))) {
            right_padding_count = right_padding - (strlen(data));
        }
        return output(output_state, ' ', left_padding_count, right_padding_count, data, strlen(data));
    }
    case FK_NAN: {
        char *data = "nan";
        size_t left_padding_count = 0;
        size_t right_padding_count = 0;
        if (left_padding > (strlen(data))) {
            left_padding_count = left_padding - (strlen(data));
        }
        if (right_padding > (strlen(data))) {
            right_padding_count = right_padding - (strlen(data));
        }
        return output(output_state, ' ', left_padding_count, right_padding_count, data, strlen(data));
    }
    }
}

/* Architecture notes, we want a high level API equivalent to src/fmt/float.rs
 * that branches on the type of float and deals with, inf, Nan and Zero
 * as separate cases, and only invoke dragon on a non-null finite number.
 *
 * This function should probably use an output function.
 *
 * Then optional wrappers may be used to sort the of the cases out.
 */

// NB, any malloc'd char* returning API will probably be snprintf twice under
// the hood
