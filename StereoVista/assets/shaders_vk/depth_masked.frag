#version 460
#extension GL_EXT_scalar_block_layout : require
// Variable indexing of the bindless array (dynamically uniform per draw, so
// no nonuniformEXT qualifier — same situation as mesh.frag).
#extension GL_EXT_nonuniform_qualifier : require

// Shared fragment stage for BOTH masked caster pipelines (sun + point): kills
// fragments below the material's alpha cutoff so cutout geometry (SLPK
// vegetation, fences, glTF MASK materials) shadows its silhouette instead of
// its full card. Must apply the SAME alpha math as mesh.frag
// (baseColor.a * texture alpha vs alphaCutoff) or casters and receivers
// disagree about which texels exist.
//
// The push block is the common prefix of DepthMaskedPush /
// DepthPointMaskedPush — model at 0, materialIndex at 64 in both (asserted in
// GpuTypes.h) — so one shader serves both pipelines. No depth output: the
// hardware depth from the vertex stage stands; discard is the only job here.

#include "scene_types.h"

layout(set = 0, binding = 2, scalar) readonly buffer MaterialsBlock {
    MaterialData uMaterials[];
};

layout(set = 1, binding = 0) uniform sampler uMaterialSampler;
layout(set = 1, binding = 1) uniform texture2D uTextures[];

layout(push_constant, scalar) uniform PushBlock { DepthMaskedPush uPush; };

layout(location = 0) in vec2 vUV;

void main() {
    MaterialData mat = uMaterials[uPush.materialIndex];
    float alpha = mat.baseColor.a;
    if (mat.albedoTexture != SV_INVALID_TEXTURE)
        alpha *= texture(sampler2D(uTextures[mat.albedoTexture], uMaterialSampler),
                         vUV).a;
    if (alpha < mat.alphaCutoff)
        discard;
}
