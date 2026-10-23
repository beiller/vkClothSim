#version 460
layout(push_constant) uniform VP {
    mat4 viewProj;
} pc;
layout(location = 0) in vec3 pos;
layout(location = 1) in vec2 uv;
layout(location = 0) out vec2 vUv;

void main() {
    vUv = uv;
    gl_Position = pc.viewProj * vec4(pos, 1.0);
}
