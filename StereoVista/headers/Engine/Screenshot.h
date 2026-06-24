#pragma once

#include <string>
#include <vector>

namespace Engine {
namespace Screenshot {

// Encode raw 8-bit pixel data to a PNG file. `channels` must be 3 (RGB) or
// 4 (RGBA). Pixel rows are expected top-to-bottom. Returns true on success.
// This is a small, self-contained PNG encoder (uncompressed deflate blocks)
// so the application gains screenshot export without an extra image-writing
// dependency.
bool writePNG(const std::string &path, int width, int height, int channels,
              const unsigned char *pixels);

// Read a rectangular region from the default framebuffer's color buffer into
// an in-memory RGB buffer (3 channels). `readBuffer` selects the source buffer
// (e.g. GL_BACK, GL_BACK_LEFT). The returned rows are flipped vertically so
// the image is upright (top-to-bottom), matching captureToPNG. `outWidth` and
// `outHeight` receive the captured dimensions. Returns true on success.
bool captureToMemory(int x, int y, int width, int height,
                     unsigned int readBuffer,
                     std::vector<unsigned char> &outPixels, int &outWidth,
                     int &outHeight);

// Read a rectangular region from the default framebuffer's color buffer and
// save it to a PNG file. `readBuffer` selects the source buffer (e.g.
// GL_BACK, GL_BACK_LEFT). The captured image is flipped vertically so the
// saved PNG appears upright. Returns true on success.
bool captureToPNG(const std::string &path, int x, int y, int width, int height,
                  unsigned int readBuffer);

// Layout for combining a stereo (two-eye) capture into the output image(s).
enum class StereoLayout {
  SideBySide, // left | right, one image of size (2*width x height)
  AboveBelow, // left on top, right below, one image of size (width x 2*height)
  Separate    // two images: "<name>_L.png" and "<name>_R.png"
};

// Combine two equally-sized RGB(A) images into a single buffer. SideBySide
// places `left` then `right` horizontally; AboveBelow places `left` above
// `right`. `outWidth`/`outHeight` receive the combined dimensions. Both inputs
// must be `width*height*channels` bytes, rows top-to-bottom. (Not valid for
// Separate, which produces two files rather than one buffer.)
std::vector<unsigned char>
combineStereo(const std::vector<unsigned char> &left,
              const std::vector<unsigned char> &right, int width, int height,
              int channels, StereoLayout layout, int &outWidth, int &outHeight);

// Capture both eyes from the default framebuffer and write a stereo-3D image.
// `leftBuffer`/`rightBuffer` select the source color buffers (e.g.
// GL_BACK_LEFT / GL_BACK_RIGHT). For SideBySide/AboveBelow a single combined
// PNG is written to `path`; for Separate two PNGs are written with "_L"/"_R"
// inserted before the extension of `path`. Returns true on success.
bool captureStereoToPNG(const std::string &path, int x, int y, int width,
                        int height, unsigned int leftBuffer,
                        unsigned int rightBuffer, StereoLayout layout);

// Build a unique, timestamped PNG path inside `directory` (created if it does
// not exist), e.g. "screenshots/StereoVista_2026-06-14_08-22-38.png".
std::string makeTimestampedPath(const std::string &directory);

} // namespace Screenshot
} // namespace Engine
