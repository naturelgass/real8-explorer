#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static bool readFile(const char* path, std::vector<uint8_t>& out, std::string& err) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        err = std::string("Failed to open ") + path;
        return false;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0) {
        fclose(f);
        err = std::string("File is empty: ") + path;
        return false;
    }
    fseek(f, 0, SEEK_SET);
    out.resize((size_t)sz);
    if (fread(out.data(), 1, out.size(), f) != out.size()) {
        fclose(f);
        err = std::string("Failed to read: ") + path;
        return false;
    }
    fclose(f);
    return true;
}

static bool writeFile(const char* path, const std::vector<uint8_t>& data, std::string& err) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        err = std::string("Failed to open for writing: ") + path;
        return false;
    }
    if (!data.empty() && fwrite(data.data(), 1, data.size(), f) != data.size()) {
        fclose(f);
        err = std::string("Failed to write: ") + path;
        return false;
    }
    fclose(f);
    return true;
}

static uint32_t readBe32(const uint8_t* p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

static bool parsePngSize(const std::vector<uint8_t>& data, uint32_t& outW, uint32_t& outH, std::string& err) {
    const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (data.size() < 24 || memcmp(data.data(), sig, sizeof(sig)) != 0) {
        err = "Not a valid PNG file.";
        return false;
    }
    const uint8_t* p = data.data() + 8;
    if (readBe32(p + 4) != 0x49484452u) { // IHDR
        err = "PNG missing IHDR.";
        return false;
    }
    outW = readBe32(p + 8);
    outH = readBe32(p + 12);
    if (outW == 0 || outH == 0 || outW > 256 || outH > 256) {
        err = "PNG dimensions must be between 1 and 256.";
        return false;
    }
    return true;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: png2ico <input.png> <output.ico>\n");
        return 1;
    }

    const char* inputPath = argv[1];
    const char* outputPath = argv[2];

    std::vector<uint8_t> pngData;
    std::string err;
    if (!readFile(inputPath, pngData, err)) {
        fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }

    uint32_t width = 0, height = 0;
    if (!parsePngSize(pngData, width, height, err)) {
        fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }

    struct IconDir {
        uint16_t reserved;
        uint16_t type;
        uint16_t count;
    };
    struct IconDirEntry {
        uint8_t width;
        uint8_t height;
        uint8_t colorCount;
        uint8_t reserved;
        uint16_t planes;
        uint16_t bitCount;
        uint32_t bytesInRes;
        uint32_t imageOffset;
    };

    IconDir dir{};
    dir.reserved = 0;
    dir.type = 1;
    dir.count = 1;

    IconDirEntry entry{};
    entry.width = (width >= 256) ? 0 : (uint8_t)width;
    entry.height = (height >= 256) ? 0 : (uint8_t)height;
    entry.colorCount = 0;
    entry.reserved = 0;
    entry.planes = 1;
    entry.bitCount = 32;
    entry.bytesInRes = (uint32_t)pngData.size();
    entry.imageOffset = sizeof(IconDir) + sizeof(IconDirEntry);

    std::vector<uint8_t> ico;
    ico.resize(entry.imageOffset + pngData.size());
    memcpy(ico.data(), &dir, sizeof(dir));
    memcpy(ico.data() + sizeof(dir), &entry, sizeof(entry));
    memcpy(ico.data() + entry.imageOffset, pngData.data(), pngData.size());

    if (!writeFile(outputPath, ico, err)) {
        fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }

    return 0;
}
