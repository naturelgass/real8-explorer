#ifndef REAL8_COMPRESSION_H
#define REAL8_COMPRESSION_H

#include <stdint.h>
#include "../hal/real8_host.h"

// Decompresses PICO-8 code into a raw buffer.
int decompress_pico8_code(IReal8Host* host, const uint8_t* input, int in_len, char* output, int out_max);

#endif