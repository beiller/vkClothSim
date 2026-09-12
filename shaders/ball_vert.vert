#version 450
layout(binding=0) buffer Ball { vec3 pos[]; } cb;
layout(binding=1) uniform UBO { mat4 viewProj; vec3 center; } ubo;
layout(location=0) out vec3 vN;

void main() {
    vec3 p = cb.pos[gl_VertexIndex];
    vN = normalize(p - ubo.center);
    gl_Position = ubo.viewProj * vec4(p, 1.0);
}
