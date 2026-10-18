#version 450

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inWorldPos;
layout(location = 2) in vec2 inUv;
layout(location = 3) in flat uint inPartId;

layout(binding = 0) uniform Ubo {
    mat4 mvp;
    mat4 model;
    vec4 lightDir;
    vec4 wireColor;
    float wireHeight;
} ubo;

layout(binding = 1) uniform sampler2D texSampler;
layout(binding = 2) uniform sampler2D vehicleTexSampler;

layout(location = 0) out vec4 outFragColor;

void main() {
    // 6: Reference Catenary wire
    if (inPartId == 6u) {
        outFragColor = vec4(1.0, 0.78, 0.22, 1.0);
        return;
    }
    // 7: Ground grid lines
    if (inPartId == 7u) {
        outFragColor = vec4(0.30, 0.33, 0.38, 1.0);
        return;
    }

    vec4 texColor;
    if (inPartId == 5u) {
        texColor = texture(vehicleTexSampler, inUv);
    } else {
        texColor = texture(texSampler, inUv);
    }

    if (texColor.a < 0.1) {
        discard;
    }

    vec3 baseColor = texColor.rgb;

    vec3 N = normalize(inNormal);
    vec3 L = normalize(ubo.lightDir.xyz);
    float diff = max(dot(N, L), 0.0);
    float ambient = 0.45;
    vec3 finalRgb = baseColor * (ambient + diff * 0.55);

    outFragColor = vec4(finalRgb, 1.0);
}
