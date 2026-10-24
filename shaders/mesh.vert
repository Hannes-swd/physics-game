#version 450
#include "common.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;  // Farbe je Eckpunkt (Gelände), sonst weiß

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vLocalPos;
layout(location = 3) flat out uint vObj;
layout(location = 4) out vec4 vColor;  // rgb Farbe (Gelände), a = 0 Rinde

void main() {
    ObjectData o = objs[gl_InstanceIndex];
    vec4 wp = o.model * vec4(inPos, 1.0);
    vWorldPos = wp.xyz;
    vNormal = mat3(o.model) * inNormal;
    vLocalPos = inPos;
    vObj = gl_InstanceIndex;
    vColor = inColor;
    gl_Position = pc.viewProj * wp;
}
