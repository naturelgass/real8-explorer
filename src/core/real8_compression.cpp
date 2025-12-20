#include "real8_compression.h"
#include <ctype.h>
#include <cstring>
#include <stdio.h>
#include <algorithm>

// --------------------------------------------------------------------------
// ROBUST BIT READER (Used for PXA)
// --------------------------------------------------------------------------
struct BitReader
{
    const uint8_t *src;
    int len;
    int pos;
    uint32_t bits;
    int bit_count;

    BitReader(const uint8_t *s, int l) : src(s), len(l), pos(0), bits(0), bit_count(0) {}

    void refill()
    {
        while (bit_count <= 24 && pos < len)
        {
            bits |= ((uint32_t)src[pos++]) << bit_count;
            bit_count += 8;
        }
    }

    uint32_t read(int n)
    {
        if (bit_count < n)
            refill();
        uint32_t val = bits & ((1UL << n) - 1);
        bits >>= n;
        bit_count -= n;
        return val;
    }
};

// --------------------------------------------------------------------------
// LEGACY (:c:) DECOMPRESSOR
// --------------------------------------------------------------------------

// Official PICO-8 Legacy LUT
// Note: Index 0 ('^') is skipped in logic (used for raw byte mode)
static const char *legacy_lut = "^\n 0123456789abcdefghijklmnopqrstuvwxyz!#%(){}[]<>+=/*:;.,~_";

// Legacy injected code strings
#define FUTURE_CODE "if(_update60)_update=function()_update60()_update60()end"
#define FUTURE_CODE2 "if(_update60)_update=function()_update60()_update_buttons()_update60()end"

static int decompress_legacy(IReal8Host *host, const uint8_t *input, int in_len, char *output, int out_max)
{
    const uint8_t *in = input;
    const uint8_t *in_end = input + in_len;
    uint8_t *out_start = (uint8_t *)output;
    uint8_t *out = out_start;

    // 1. Validate Header (:c:\0)
    // Header is 4 bytes.
    if (in_len < 8)
        return -1;
    // We assume the caller checked the signature, but we skip it here.
    in += 4;

    // 2. Read Uncompressed Length (2 bytes, Big Endian)
    int val_high = *in++;
    int val_low = *in++;
    int target_len = (val_high * 256) + val_low;

    // 3. Read Compressed Length (2 bytes - unused in official decompressor logic)
    in += 2;

    if (target_len > out_max - 1)
        target_len = out_max - 1;

    // Clear output buffer
    memset(output, 0, out_max);

    // 4. Decompression Loop
    while (out < out_start + target_len && in < in_end)
    {
        uint8_t val = *in++;

        // Literal Mode (< 60)
        if (val < 60)
        {
            if (val == 0)
            {
                // Rare literal: Read next byte raw
                if (in >= in_end)
                    break; // Existing check is good
                *out++ = *in++;
            }
            else
            {
                *out++ = (uint8_t)legacy_lut[val];
            }
        }
        // Block Copy Mode (LZ)
        else
        {
            // SAFETY Check if we have the 2nd byte available
            if (in >= in_end)
                break;

            uint8_t val2 = *in++;

            // Calculate Offset and Length
            int block_offset = (val - 60) * 16 + (val2 & 0x0F);
            int block_length = (val2 >> 4) + 2;

            uint8_t *src = out - block_offset;

            // Safety Check
            if (src < out_start)
            {
                // In corrupted carts, this might happen. Treat as 0 or bail.
                // We fill with 0 to match safe behavior.
                for (int i = 0; i < block_length; ++i)
                {
                    if (out >= out_start + out_max)
                        break;
                    *out++ = 0;
                }
            }
            else
            {
                // Copy loop (Must be byte-by-byte for RLE overlaps)
                for (int i = 0; i < block_length; ++i)
                {
                    if (out >= out_start + out_max)
                        break;
                    *out++ = *src++;
                }
            }
        }
    }

    // 5. Strip Future Code
    // PICO-8 0.1.8+ injects code at the end for 60fps support on older versions.
    // The decompressor must remove this to restore the original code exactly.
    int current_len = (int)(out - out_start);

    auto check_and_strip = [&](const char *code_str)
    {
        int code_len = strlen(code_str);
        if (current_len >= code_len)
        {
            char *end_ptr = (char *)out_start + current_len - code_len;
            if (memcmp(end_ptr, code_str, code_len) == 0)
            {
                *end_ptr = 0; // Truncate
                current_len -= code_len;
                out -= code_len;
            }
        }
    };

    check_and_strip(FUTURE_CODE);
    check_and_strip(FUTURE_CODE2);

    // Null terminate
    if (current_len < out_max)
        out_start[current_len] = 0;

    // Log preview
    /*
    char preview[65];
    int p_len = (current_len < 64) ? current_len : 64;
    for(int i=0; i<p_len; i++) {
        char c = output[i];
        preview[i] = (c >= 32 && c <= 126) ? c : (c == '\n' ? '|' : '.');
    }
    preview[p_len] = 0;

    char msg[128];
    snprintf(msg, sizeof(msg), "[LEGACY RESULT] %s", preview);
    host->log(msg);
    */

    return current_len;
}

// --------------------------------------------------------------------------
// PXA DECOMPRESSOR
// --------------------------------------------------------------------------
static int decompress_pxa(IReal8Host *host, const uint8_t *input, int in_len, char *output, int out_max)
{
    if (in_len < 8)
        return -1;
    host->log("PXA COMPRESSION FOUND!");

    int dest_len = (input[4] << 8) | input[5];
    if (dest_len > out_max - 1)
        dest_len = out_max - 1;

    uint8_t mtf[256];
    for (int i = 0; i < 256; ++i)
        mtf[i] = (uint8_t)i;

    BitReader br(input + 8, in_len - 8);
    int op = 0;
    unsigned long last_yield = host->getMillis();

    while (op < dest_len)
    {
        // IMPROVEMENT 1: Only check system timer every 2048 operations
        if ((op & 2047) == 0)
        {
            if (host->getMillis() - last_yield > 10)
            {
                host->delayMs(0);
                last_yield = host->getMillis();
            }
        }

        if (br.read(1))
        {
            // Literal Logic
            int nbits = 4;
            while (br.read(1))
            {
                nbits++;
                if (nbits > 16)
                    return -1; // Safety against infinite 1s
            }
            int idx = br.read(nbits) + (1 << nbits) - 16;

            // IMPROVEMENT 2: Validate index
            if (idx > 255)
            {
                host->log("[PXA] Corrupt Literal");
                return -1;
            }

            uint8_t val = mtf[idx];

            // IMPROVEMENT 3: memmove for clarity/speed
            if (idx > 0)
            {
                memmove(mtf + 1, mtf, idx);
            }
            mtf[0] = val;
            output[op++] = (char)val;
        }
        else
        {
            // Block Logic
            int offset_bits;
            if (br.read(1))
            {
                if (br.read(1))
                    offset_bits = 5;
                else
                    offset_bits = 10;
            }
            else
            {
                offset_bits = 15;
            }

            int offset = br.read(offset_bits) + 1;

            // Raw Block (Official specific check)
            if (offset_bits == 10 && offset == 1)
            {
                while (true)
                {
                    uint8_t val = (uint8_t)br.read(8);
                    if (val == 0)
                        break;
                    if (op < dest_len)
                        output[op++] = (char)val;
                }
                continue;
            }

            int len = 3;
            int part;
            do
            {
                part = br.read(3);
                len += part;
            } while (part == 7 && op + len < dest_len); // Add bounds check to loop

            int src = op - offset;
            if (src < 0)
                src = 0; // Safety clamp

            // Optimization: if offset is 1 (RLE), memset is faster
            if (offset == 1)
            {
                int run = std::min(len, dest_len - op);
                memset(output + op, output[src], run);
                op += run;
            }
            else
            {
                for (int i = 0; i < len && op < dest_len; ++i)
                {
                    output[op++] = output[src + i];
                }
            }
        }
    }
    output[op] = 0;
    return op;
}

// --------------------------------------------------------------------------
// MAIN ENTRY
// --------------------------------------------------------------------------
int decompress_pico8_code(IReal8Host *host, const uint8_t *input, int in_len, char *output, int out_max)
{
    if (in_len < 8)
        return -1;

    // 1. PXA Check (00 70 78 61 = \0 p x a)
    if (input[0] == 0x00 && input[1] == 0x70 && input[2] == 0x78 && input[3] == 0x61)
    {
        return decompress_pxa(host, input, in_len, output, out_max);
    }

    // 2. Legacy Check (: c : \0)
    // Header is 3a 63 3a 00
    if (input[0] == 0x3a && input[1] == 0x63 && input[2] == 0x3a && input[3] == 0x00)
    {
        return decompress_legacy(host, input, in_len, output, out_max);
    }

    // 3. Fallback: Plain Text / Uncompressed
    // Some carts are saved as plain text .p8 (or just lua code).
    bool looks_like_text = true;
    for (int i = 0; i < (in_len < 10 ? in_len : 10); i++)
    {
        // Allow tabs, newlines, and standard printable chars.
        if ((input[i] < 0x09) && input[i] != 0)
        {
            looks_like_text = false;
            break;
        }
    }

    if (looks_like_text)
    {
        int copy_len = (in_len < out_max) ? in_len : out_max - 1;
        for (int i = 0; i < copy_len; ++i)
        {
            output[i] = (char)input[i];
        }
        output[copy_len] = 0;
        return copy_len;
    }

    host->log("[REAL8-ERROR] Unknown compression format.");
    return -1;
}