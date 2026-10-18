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
        outFragColor = vec4(0.20, 0.23, 0.30, 1.0);
        return;
    }
    // 8: Pivot p1 (Base) - Vivid Cyan
    if (inPartId == 8u) {
        outFragColor = vec4(0.0, 0.85, 1.0, 1.0);
        return;
    }
    // 9: Pivot p2 (Elbow) - Vivid Orange
    if (inPartId == 9u) {
        outFragColor = vec4(1.0, 0.55, 0.0, 1.0);
        return;
    }
    // 10: Pivot p3 (Tip/Head) - Neon Green
    if (inPartId == 10u) {
        outFragColor = vec4(0.1, 1.0, 0.35, 1.0);
        return;
    }
    // 11: Pivot p4_base (Coupler Base) - Magenta
    if (inPartId == 11u) {
        outFragColor = vec4(0.9, 0.2, 1.0, 1.0);
        return;
    }
    // 12: Pivot p4_elbow (Coupler Elbow) - Coral Pink
    if (inPartId == 12u) {
        outFragColor = vec4(1.0, 0.25, 0.6, 1.0);
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
    if (!gl_FrontFacing) {
        N = -N;
    }
    vec3 L = normalize(ubo.lightDir.xyz);
    float diff = max(dot(N, L), 0.0);
    vec3 fillL = normalize(vec3(-ubo.lightDir.x, 0.3, -ubo.lightDir.z));
    float fillDiff = max(dot(N, fillL), 0.0) * 0.25;
    float ambient = 0.65;
    vec3 finalRgb = baseColor * min(ambient + diff * 0.45 + fillDiff, 1.15);

    outFragColor = vec4(finalRgb, 1.0);
}
