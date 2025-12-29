#pragma once

#include <stdint.h>

#define CART_BLOB_MAGIC "P8GB"
#define CART_BLOB_MAGIC_SIZE 4
#define CART_BLOB_FLAG_NONE 0u

struct CartBlobHeader {
    char magic[CART_BLOB_MAGIC_SIZE];
    uint32_t flags;
    uint32_t raw_size;
    uint32_t comp_size;
};
