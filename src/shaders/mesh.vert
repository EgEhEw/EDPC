#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in uint inPartId;

layout(binding = 0) uniform Ubo {
    mat4 mvp;
    mat4 model;
    vec4 lightDir;
    vec4 wireColor;
    float wireHeight;
} ubo;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec3 outWorldPos;
layout(location = 2) out vec2 outUv;
layout(location = 3) out flat uint outPartId;

void main() {
    vec4 worldPos = ubo.model * vec4(inPos, 1.0);
    gl_Position = ubo.mvp * vec4(inPos, 1.0);
    outNormal = mat3(ubo.model) * inNormal;
    outWorldPos = worldPos.xyz;
    outUv = inUv;
    outPartId = inPartId;
}
