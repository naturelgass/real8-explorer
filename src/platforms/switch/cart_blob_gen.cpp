#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#include "cart_blob.h"

static void printUsage(const char* exe) {
    const char* name = (exe && *exe) ? exe : "cart_blob_gen";
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s --template <output.bin> <payload_capacity_bytes>\n", name);
}

static bool writeTemplateBlob(const char* output, uint32_t payloadCapacity, std::string& err) {
    if (payloadCapacity < 1) {
        err = "Payload capacity must be > 0.";
        return false;
    }

    FILE* out = fopen(output, "wb");
    if (!out) {
        err = std::string("Failed to open ") + output + " for writing";
        return false;
    }

    CartBlobHeader header{};
    memcpy(header.magic, CART_BLOB_MAGIC, CART_BLOB_MAGIC_SIZE);
    header.flags = CART_BLOB_FLAG_NONE;
    header.raw_size = 0;
    header.comp_size = payloadCapacity;

    if (fwrite(&header, 1, sizeof(header), out) != sizeof(header)) {
        fclose(out);
        err = "Failed to write template header.";
        return false;
    }

    std::vector<uint8_t> zeros;
    zeros.resize(4096, 0);
    uint32_t remaining = payloadCapacity;
    while (remaining) {
        uint32_t chunk = remaining > (uint32_t)zeros.size() ? (uint32_t)zeros.size() : remaining;
        if (fwrite(zeros.data(), 1, chunk, out) != chunk) {
            fclose(out);
            err = "Failed to write template padding.";
            return false;
        }
        remaining -= chunk;
    }

    fclose(out);
    return true;
}

int main(int argc, char** argv) {
    if (argc < 4 || strcmp(argv[1], "--template") != 0) {
        printUsage(argc > 0 ? argv[0] : nullptr);
        return 1;
    }

    const char* output = argv[2];
    const char* capStr = argv[3];
    char* endp = nullptr;
    unsigned long cap = strtoul(capStr, &endp, 0);
    if (!endp || endp == capStr || *endp != '\0') {
        fprintf(stderr, "Invalid capacity: %s\n", capStr);
        return 1;
    }

    std::string err;
    if (!writeTemplateBlob(output, (uint32_t)cap, err)) {
        fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }

    return 0;
}
