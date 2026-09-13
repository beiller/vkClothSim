#version 450

struct Vtx { vec3 pos; vec3 nrm; vec3 col; };
layout(binding = 0) buffer Vtxs { Vtx v[]; } vtx;
layout(binding = 1) uniform UBO { mat4 viewProj; } ubo;

layout(location = 0) out vec3 vN;
layout(location = 1) out vec3 vCol;

void main() {
    Vtx p = vtx.v[gl_VertexIndex];
    vN = p.nrm;
    vCol = p.col;
    gl_Position = ubo.viewProj * vec4(p.pos, 1.0);
}
