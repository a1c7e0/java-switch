#include <stdio.h>
#include <string.h>

#include "imageDecompressor.hpp"
#include "endian.hpp"
#include "zlib.h"

static void put_u4(unsigned char* target, unsigned int value) {
    target[0] = static_cast<unsigned char>(value);
    target[1] = static_cast<unsigned char>(value >> 8);
    target[2] = static_cast<unsigned char>(value >> 16);
    target[3] = static_cast<unsigned char>(value >> 24);
}

static void put_u8(unsigned char* target, unsigned long long value) {
    for (unsigned int i = 0; i < 8; i++) {
        target[i] = static_cast<unsigned char>(value >> (i * 8));
    }
}

int main() {
    static const char expected[] =
        "A pinned OpenJDK image resource uses the linked ZIP_InflateFully path.";
    unsigned char compressed[256];
    unsigned char resource[29 + sizeof(compressed)] = {};
    unsigned char output[sizeof(expected)];
    uLongf compressed_size = sizeof(compressed);

    if (compress2(compressed, &compressed_size,
                  reinterpret_cast<const Bytef*>(expected), sizeof(expected),
                  Z_BEST_COMPRESSION) != Z_OK) {
        return 1;
    }

    /* Horizon branch binds this call to upstream libzip's real C definition. */
    ImageDecompressor::image_decompressor_init();
    put_u4(resource, ResourceHeader::resource_header_magic);
    put_u8(resource + 4, compressed_size);
    put_u8(resource + 12, sizeof(expected));
    put_u4(resource + 20, 0);
    put_u4(resource + 24, 0);
    resource[28] = 1;
    memcpy(resource + 29, compressed, compressed_size);
    const char strings_data[] = "zip";
    const ImageStrings strings(reinterpret_cast<u1*>(const_cast<char*>(strings_data)),
                                sizeof(strings_data));
    ImageDecompressor::decompress_resource(resource, output, sizeof(output),
                                           &strings, Endian::get_handler(false));
    if (memcmp(output, expected, sizeof(expected)) != 0) return 3;
    return 0;
}
