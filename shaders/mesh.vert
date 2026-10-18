#version 450
#include "common.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vLocalPos;
layout(location = 3) flat out uint vObj;

void main() {
    ObjectData o = objs[gl_InstanceIndex];
    vec4 wp = o.model * vec4(inPos, 1.0);
    vWorldPos = wp.xyz;
    vNormal = mat3(o.model) * inNormal;
    vLocalPos = inPos;
    vObj = gl_InstanceIndex;
    gl_Position = pc.viewProj * wp;
}
