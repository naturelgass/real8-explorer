#pragma once

#include <stdint.h>

#define CART_BLOB_MAGIC "P8GB"
#define CART_BLOB_MAGIC_SIZE 4

enum CartBlobFlags : uint32_t {
    CART_BLOB_FLAG_NONE = 0u,
    CART_BLOB_FLAG_STRETCH = 1u << 0,
    CART_BLOB_FLAG_CRTFILTER = 1u << 1,
    CART_BLOB_FLAG_INTERPOL8 = 1u << 2
};

struct CartBlobHeader {
    char magic[CART_BLOB_MAGIC_SIZE];
    uint32_t flags;
    uint32_t raw_size;
    uint32_t comp_size;
};
