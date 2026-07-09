// Scratch encoder tool for the SLPK test harness: turns a raw point set into
// the LEPCC blobs synthetic_pcsl20.slpk embeds (see make_synthetic_slpk.py,
// which drives this per node). Compiled ad hoc against the vendored decoder+
// encoder sources — it is NOT part of the app build:
//
//   g++ -O2 -std=c++17 -I ../headers/libs \
//       make_lepcc_blobs.cpp ../headers/libs/lepcc/*.cpp -o make_lepcc_blobs
//
// (cl.exe works the same way on Windows; any C++17 compiler will do.)
//
// Input file:  u32 count | count*3 f64 xyz (layer SR) | count*3 u8 rgb |
//              count u16 intensity | count u8 classCode
// Output file: u32 count | u32 size + blob (xyz) | u32 size + blob (rgb) |
//              u32 size + blob (intensity) | the four input arrays REORDERED
//              into LEPCC storage order (the encoder resorts points; every
//              per-point column and the expected-value sidecar must follow
//              that order).
//
// Tolerances: 1e-8 deg in x/y (~1 mm at the equator) and 1 mm in z — tighter
// than the harness's 1 cm assertion by an order of magnitude.

#include <lepcc/include/lepcc_c_api.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

template <typename T>
bool readArray(std::FILE* f, std::vector<T>& out, size_t count) {
    out.resize(count);
    return count == 0 || std::fread(out.data(), sizeof(T), count, f) == count;
}

template <typename T>
void writeArray(std::FILE* f, const std::vector<T>& v) {
    if (!v.empty())
        std::fwrite(v.data(), sizeof(T), v.size(), f);
}

void writeBlob(std::FILE* f, const std::vector<unsigned char>& blob) {
    const uint32_t size = static_cast<uint32_t>(blob.size());
    std::fwrite(&size, sizeof(size), 1, f);
    writeArray(f, blob);
}

[[noreturn]] void fail(const char* what) {
    std::fprintf(stderr, "make_lepcc_blobs: %s\n", what);
    std::exit(1);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3)
        fail("usage: make_lepcc_blobs <in.bin> <out.bin>");

    std::FILE* in = std::fopen(argv[1], "rb");
    if (!in)
        fail("cannot open input");
    uint32_t count = 0;
    if (std::fread(&count, sizeof(count), 1, in) != 1 || count == 0)
        fail("bad point count");
    std::vector<double> xyz;
    std::vector<uint8_t> rgb;
    std::vector<uint16_t> intensity;
    std::vector<uint8_t> classCodes;
    if (!readArray(in, xyz, size_t(count) * 3) ||
        !readArray(in, rgb, size_t(count) * 3) ||
        !readArray(in, intensity, count) || !readArray(in, classCodes, count))
        fail("truncated input");
    std::fclose(in);

    lepcc_ContextHdl ctx = lepcc_createContext();
    if (!ctx)
        fail("lepcc context");

    // XYZ (the encoder resorts the points; orderOut records the permutation).
    std::vector<unsigned int> order(count);
    unsigned int xyzBytes = 0;
    if (lepcc_computeCompressedSizeXYZ(ctx, count, xyz.data(), 1e-8, 1e-8, 1e-3,
                                       &xyzBytes, order.data()) != 0)
        fail("computeCompressedSizeXYZ");
    std::vector<unsigned char> xyzBlob(xyzBytes);
    unsigned char* cursor = xyzBlob.data();
    if (lepcc_encodeXYZ(ctx, &cursor, static_cast<int>(xyzBytes)) != 0)
        fail("encodeXYZ");

    // Reorder every column into storage order before encoding/writing.
    std::vector<double> xyzSorted(size_t(count) * 3);
    std::vector<uint8_t> rgbSorted(size_t(count) * 3);
    std::vector<uint16_t> intensitySorted(count);
    std::vector<uint8_t> classSorted(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t src = order[i];
        for (int c = 0; c < 3; ++c) {
            xyzSorted[size_t(i) * 3 + c] = xyz[size_t(src) * 3 + c];
            rgbSorted[size_t(i) * 3 + c] = rgb[size_t(src) * 3 + c];
        }
        intensitySorted[i] = intensity[src];
        classSorted[i] = classCodes[src];
    }

    unsigned int rgbBytes = 0;
    if (lepcc_computeCompressedSizeRGB(ctx, count, rgbSorted.data(), &rgbBytes) != 0)
        fail("computeCompressedSizeRGB");
    std::vector<unsigned char> rgbBlob(rgbBytes);
    cursor = rgbBlob.data();
    if (lepcc_encodeRGB(ctx, &cursor, static_cast<int>(rgbBytes)) != 0)
        fail("encodeRGB");

    unsigned int intBytes = 0;
    if (lepcc_computeCompressedSizeIntensity(ctx, count, intensitySorted.data(),
                                             &intBytes) != 0)
        fail("computeCompressedSizeIntensity");
    std::vector<unsigned char> intBlob(intBytes);
    cursor = intBlob.data();
    if (lepcc_encodeIntensity(ctx, &cursor, static_cast<int>(intBytes),
                              intensitySorted.data(), count) != 0)
        fail("encodeIntensity");

    lepcc_deleteContext(&ctx);

    std::FILE* out = std::fopen(argv[2], "wb");
    if (!out)
        fail("cannot open output");
    std::fwrite(&count, sizeof(count), 1, out);
    writeBlob(out, xyzBlob);
    writeBlob(out, rgbBlob);
    writeBlob(out, intBlob);
    writeArray(out, xyzSorted);
    writeArray(out, rgbSorted);
    writeArray(out, intensitySorted);
    writeArray(out, classSorted);
    std::fclose(out);
    return 0;
}
