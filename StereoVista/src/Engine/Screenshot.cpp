#include "Engine/Screenshot.h"

#include <glad/glad.h>

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace Engine {
namespace Screenshot {

namespace {

// ---- CRC32 (used for PNG chunk checksums) ----
uint32_t crc32Update(uint32_t crc, const unsigned char *data, size_t len) {
  static uint32_t table[256];
  static bool tableBuilt = false;
  if (!tableBuilt) {
    for (uint32_t n = 0; n < 256; ++n) {
      uint32_t c = n;
      for (int k = 0; k < 8; ++k)
        c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      table[n] = c;
    }
    tableBuilt = true;
  }
  crc ^= 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i)
    crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

// ---- Adler32 (used for the zlib stream wrapping the image data) ----
uint32_t adler32(const unsigned char *data, size_t len) {
  const uint32_t MOD = 65521u;
  uint32_t a = 1, b = 0;
  for (size_t i = 0; i < len; ++i) {
    a = (a + data[i]) % MOD;
    b = (b + a) % MOD;
  }
  return (b << 16) | a;
}

void putBE32(std::vector<unsigned char> &out, uint32_t v) {
  out.push_back((v >> 24) & 0xFF);
  out.push_back((v >> 16) & 0xFF);
  out.push_back((v >> 8) & 0xFF);
  out.push_back(v & 0xFF);
}

void writeChunk(std::vector<unsigned char> &out, const char type[4],
                const std::vector<unsigned char> &data) {
  putBE32(out, static_cast<uint32_t>(data.size()));
  size_t crcStart = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  uint32_t crc = crc32Update(0, out.data() + crcStart, out.size() - crcStart);
  putBE32(out, crc);
}

// Wrap raw bytes in a zlib stream using uncompressed (stored) deflate blocks.
std::vector<unsigned char> zlibStore(const std::vector<unsigned char> &raw) {
  std::vector<unsigned char> out;
  out.push_back(0x78); // CMF: deflate, 32K window
  out.push_back(0x01); // FLG: no preset dict, fastest
  const size_t MAX_BLOCK = 65535;
  size_t pos = 0;
  if (raw.empty()) {
    // Single empty final stored block.
    out.push_back(0x01);
    out.push_back(0x00);
    out.push_back(0x00);
    out.push_back(0xFF);
    out.push_back(0xFF);
  }
  while (pos < raw.size()) {
    size_t blockLen = (raw.size() - pos < MAX_BLOCK) ? (raw.size() - pos)
                                                     : MAX_BLOCK;
    bool last = (pos + blockLen) >= raw.size();
    out.push_back(last ? 0x01 : 0x00);
    uint16_t len = static_cast<uint16_t>(blockLen);
    uint16_t nlen = static_cast<uint16_t>(~len);
    out.push_back(len & 0xFF);
    out.push_back((len >> 8) & 0xFF);
    out.push_back(nlen & 0xFF);
    out.push_back((nlen >> 8) & 0xFF);
    out.insert(out.end(), raw.begin() + pos, raw.begin() + pos + blockLen);
    pos += blockLen;
  }
  putBE32(out, adler32(raw.data(), raw.size()));
  return out;
}

} // namespace

bool writePNG(const std::string &path, int width, int height, int channels,
              const unsigned char *pixels) {
  if (!pixels || width <= 0 || height <= 0 ||
      (channels != 3 && channels != 4)) {
    std::cerr << "writePNG: invalid arguments" << std::endl;
    return false;
  }

  // Build the raw (filtered) image: a 0x00 filter byte per scanline followed by
  // that row's pixel data.
  std::vector<unsigned char> raw;
  raw.reserve(static_cast<size_t>(height) * (1 + width * channels));
  for (int y = 0; y < height; ++y) {
    raw.push_back(0x00); // filter: none
    const unsigned char *row =
        pixels + static_cast<size_t>(y) * width * channels;
    raw.insert(raw.end(), row, row + static_cast<size_t>(width) * channels);
  }

  std::vector<unsigned char> png;
  const unsigned char sig[8] = {0x89, 0x50, 0x4E, 0x47,
                                0x0D, 0x0A, 0x1A, 0x0A};
  png.insert(png.end(), sig, sig + 8);

  // IHDR
  std::vector<unsigned char> ihdr;
  putBE32(ihdr, static_cast<uint32_t>(width));
  putBE32(ihdr, static_cast<uint32_t>(height));
  ihdr.push_back(8);                              // bit depth
  ihdr.push_back(channels == 4 ? 6 : 2);          // color type: RGBA / RGB
  ihdr.push_back(0);                              // compression
  ihdr.push_back(0);                              // filter
  ihdr.push_back(0);                              // interlace
  writeChunk(png, "IHDR", ihdr);

  // IDAT
  writeChunk(png, "IDAT", zlibStore(raw));

  // IEND
  writeChunk(png, "IEND", std::vector<unsigned char>());

  std::error_code ec;
  std::filesystem::path p(path);
  if (p.has_parent_path())
    std::filesystem::create_directories(p.parent_path(), ec);

  std::ofstream file(path, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "writePNG: failed to open " << path << std::endl;
    return false;
  }
  file.write(reinterpret_cast<const char *>(png.data()), png.size());
  file.close();
  return file.good();
}

bool captureToMemory(int x, int y, int width, int height,
                     unsigned int readBuffer,
                     std::vector<unsigned char> &outPixels, int &outWidth,
                     int &outHeight) {
  if (width <= 0 || height <= 0)
    return false;

  // Read from the default framebuffer's selected color buffer, restoring all
  // touched GL state afterwards so the capture is transparent to the renderer.
  GLint prevReadFbo = 0;
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  GLint prevReadBuffer = GL_BACK;
  glGetIntegerv(GL_READ_BUFFER, &prevReadBuffer);
  glReadBuffer(static_cast<GLenum>(readBuffer));

  const int channels = 3; // RGB
  std::vector<unsigned char> pixels(static_cast<size_t>(width) * height *
                                    channels);

  GLint prevAlign = 4;
  glGetIntegerv(GL_PACK_ALIGNMENT, &prevAlign);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(x, y, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
  glPixelStorei(GL_PACK_ALIGNMENT, prevAlign);

  glReadBuffer(static_cast<GLenum>(prevReadBuffer));
  glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFbo);

  // OpenGL returns rows bottom-to-top; flip so the image is upright.
  const size_t rowBytes = static_cast<size_t>(width) * channels;
  outPixels.assign(pixels.size(), 0);
  for (int row = 0; row < height; ++row) {
    std::copy(pixels.begin() + static_cast<size_t>(height - 1 - row) * rowBytes,
              pixels.begin() + static_cast<size_t>(height - row) * rowBytes,
              outPixels.begin() + static_cast<size_t>(row) * rowBytes);
  }
  outWidth = width;
  outHeight = height;
  return true;
}

bool captureToPNG(const std::string &path, int x, int y, int width, int height,
                  unsigned int readBuffer) {
  std::vector<unsigned char> flipped;
  int w = 0, h = 0;
  if (!captureToMemory(x, y, width, height, readBuffer, flipped, w, h))
    return false;

  return writePNG(path, w, h, 3, flipped.data());
}

std::string makeTimestampedPath(const std::string &directory) {
  std::error_code ec;
  std::filesystem::create_directories(directory, ec);

  std::time_t t = std::time(nullptr);
  std::tm tmBuf{};
#if defined(_WIN32)
  localtime_s(&tmBuf, &t);
#else
  localtime_r(&t, &tmBuf);
#endif
  std::ostringstream name;
  name << "StereoVista_" << std::put_time(&tmBuf, "%Y-%m-%d_%H-%M-%S")
       << ".png";

  std::filesystem::path p(directory);
  p /= name.str();
  return p.string();
}

} // namespace Screenshot
} // namespace Engine
