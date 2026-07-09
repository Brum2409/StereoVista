// Hand-written stand-in for the CMake-generated draco_features.h (draco
// v1.5.7 — vendored decoder subset, see docs/SLPK_IMPLEMENTATION_PLAN.md §3).
// Mirrors the upstream desktop-default feature set with every encoder-only
// and glTF-transcoder feature left off:
//   * mesh + point-cloud DECODE support, standard + predictive edgebreaker,
//     normal (octahedron) encoding, legacy-bitstream backwards compatibility;
//   * attribute deduplication kept (upstream enables it for the C++ decoder
//     unconditionally — see cmake/draco_options.cmake);
//   * DRACO_TRANSCODER_SUPPORTED deliberately NOT defined (glTF transcoder,
//     scene/material machinery — not vendored).
#ifndef DRACO_FEATURES_H_
#define DRACO_FEATURES_H_

#define DRACO_MESH_COMPRESSION_SUPPORTED
#define DRACO_POINT_CLOUD_COMPRESSION_SUPPORTED
#define DRACO_NORMAL_ENCODING_SUPPORTED
#define DRACO_STANDARD_EDGEBREAKER_SUPPORTED
#define DRACO_PREDICTIVE_EDGEBREAKER_SUPPORTED
#define DRACO_BACKWARDS_COMPATIBILITY_SUPPORTED
#define DRACO_ATTRIBUTE_INDICES_DEDUPLICATION_SUPPORTED
#define DRACO_ATTRIBUTE_VALUES_DEDUPLICATION_SUPPORTED

#endif  // DRACO_FEATURES_H_
