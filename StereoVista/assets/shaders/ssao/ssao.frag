#version 460
out float FragColor;

in vec2 TexCoords;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D texNoise;

uniform vec3 samples[64];
uniform mat4 projection;
uniform vec2 noiseScale; // screen dimensions / noise texture size
uniform int kernelSize;
uniform float radius;
uniform float bias;
uniform float power;

void main()
{
    // Retrieve view-space position and normal for this fragment
    vec4 fragData = texture(gPosition, TexCoords);

    // Skip fragments with no geometry (w == 0 means background)
    if (fragData.w < 0.5) {
        FragColor = 1.0;
        return;
    }

    vec3 fragPos = fragData.xyz;
    vec3 normal = normalize(texture(gNormal, TexCoords).rgb);
    vec3 randomVec = normalize(texture(texNoise, TexCoords * noiseScale).xyz);

    // Create TBN change-of-basis matrix: from tangent-space to view-space
    // Using the Gramm-Schmidt process to re-orthogonalize
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    // Iterate over the sample kernel and calculate occlusion factor
    float occlusion = 0.0;
    for (int i = 0; i < kernelSize; ++i)
    {
        // Get sample position (transform from tangent to view-space)
        vec3 samplePos = TBN * samples[i];
        samplePos = fragPos + samplePos * radius;

        // Project sample position to screen space to get texture coordinates
        vec4 offset = vec4(samplePos, 1.0);
        offset = projection * offset;       // from view to clip-space
        offset.xyz /= offset.w;             // perspective divide
        offset.xyz = offset.xyz * 0.5 + 0.5; // transform to range [0, 1]

        // Get sample depth from the G-buffer position texture
        float sampleDepth = texture(gPosition, offset.xy).z;

        // Range check & accumulate:
        // smoothstep prevents contributions from geometry that is too far away
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampleDepth));
        occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    occlusion = 1.0 - (occlusion / float(kernelSize));

    // Apply power curve for artistic control
    FragColor = pow(occlusion, power);
}
