#version 450
#include "common.glsl"

layout(location = 0) in vec3 inPos;

void main() {
    gl_Position = pc.viewProj * objs[gl_InstanceIndex].model * vec4(inPos, 1.0);
}
