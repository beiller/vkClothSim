#version 450
// The ONE mesh vertex shader. Every mesh (ground, capsules, cloth, ball) is drawn through it:
// the vertices are indexed into a Vtx SSBO (binding 0) — there is no per-mesh vertex input.
// `vtx` is (pos, nrm, col) = 48 bytes (std430: three 16-byte vec3 blocks). The sim writes
// .pos/.nrm; the CPU bakes the ground + capsules; .col is the per-mesh albedo.
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
