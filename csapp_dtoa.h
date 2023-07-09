//
// Created by Guillaume DIDIER on 21/05/2023.
//

#ifndef CSAPP_DTOA_H
#define CSAPP_DTOA_H

// This is the minimum size for a buffer storing the decimal digits of a double
// guaranteeing that the round trip works.
//
// The exact formula is `ceil(# bits in mantissa * log_10 2 + 1)`.
#define MAX_SIG_DIGIT 17
// TODO, double check if we need to add an extra slot for null termination.

#define DTOA_EXACT_BUFFER_SIZE 1024

typedef enum {
    FORMAT_f,
    FORMAT_F,
    FORMAT_g,
    FORMAT_G,
    /* ... TODO */
} dtoa_flags_t;

ssize_t sio_format_double_shortest(sio_output_function output,
                                   void *output_state, double d,
                                   dtoa_flags_t flags, size_t left_padding);

ssize_t sio_format_double_exact(sio_output_function output, void *output_state,
                                double d, dtoa_flags_t flags,
                                size_t left_padding, int precision);

#endif // CSAPP_DTOA_H
