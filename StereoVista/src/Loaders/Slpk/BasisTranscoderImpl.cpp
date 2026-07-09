// ============================================================================
// Vendored basis_universal transcoder — single implementation TU.
// ----------------------------------------------------------------------------
// Compiles headers/libs/basisu/transcoder/basisu_transcoder.cpp with the
// app's configuration (the StbImageImpl pattern: defines live HERE, next to
// the include, so they can never drift from the build). Everything the app
// needs: KTX2 container + zstd supercompression, ETC1S + UASTC -> BC7/BC5/
// RGBA32. The mobile-only target formats are compiled out — nothing in a
// desktop Vulkan app transcodes TO PVRTC/ATC/FXT1.
//
// zstd itself is the sibling single-file decoder headers/libs/basisu/zstd/
// zstddeclib.c (own TU, compiles as C).
//
// Consumers (I3STexture.cpp) must include basisu_transcoder.h with the same
// BASISD_SUPPORT_KTX2* values — those two gate header-visible declarations.
// ============================================================================

#define BASISD_SUPPORT_KTX2 1
#define BASISD_SUPPORT_KTX2_ZSTD 1
#define BASISD_SUPPORT_PVRTC1 0
#define BASISD_SUPPORT_ATC 0
#define BASISD_SUPPORT_FXT1 0
#define BASISD_SUPPORT_PVRTC2 0

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4018 4127 4189 4244 4245 4267 4310 4324 4457 4458 4459 4701 4702 4996)
#endif
#include <basisu/transcoder/basisu_transcoder.cpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
