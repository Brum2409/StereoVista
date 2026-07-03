#pragma once

#include <string>
#include <vector>

namespace Engine {
namespace Screenshot {

// Graphics-API-agnostic screenshot support: a self-contained PNG encoder,
// stereo-pair composition, and timestamped output paths. The capture side
// (which used to be glReadPixels here) now lives in the Vulkan renderer
// (renderer::Renderer::requestScreenshot), which reads the presented frame
// back through a copy-to-buffer and hands the pixels to writePNG.

// Encode raw 8-bit pixel data to a PNG file. `channels` must be 3 (RGB) or
// 4 (RGBA). Pixel rows are expected top-to-bottom. Returns true on success.
// Small, self-contained encoder (uncompressed deflate blocks) so screenshot
// export needs no extra image-writing dependency.
bool writePNG(const std::string &path, int width, int height, int channels,
              const unsigned char *pixels);

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

// Build a unique, timestamped PNG path inside `directory` (created if it does
// not exist), e.g. "screenshots/StereoVista_2026-06-14_08-22-38.png".
std::string makeTimestampedPath(const std::string &directory);

} // namespace Screenshot
} // namespace Engine
