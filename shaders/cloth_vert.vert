#version 450
layout(binding=0) buffer Cloth { vec3 pos[]; } cb;
layout(binding=1) uniform UBO { mat4 viewProj; int W; int H; } ubo;
layout(location=0) out vec3 vN;

void main() {
    int n = gl_VertexIndex;
    int i = n % ubo.W;
    int j = n / ubo.W;
    vec3 p = cb.pos[n];
    vec3 left  = (i > 0)         ? cb.pos[n - 1]     : p;
    vec3 right = (i < ubo.W - 1) ? cb.pos[n + 1]     : p;
    vec3 up    = (j > 0)         ? cb.pos[n - ubo.W] : p;
    vec3 down  = (j < ubo.H - 1) ? cb.pos[n + ubo.W] : p;
    vN = normalize(cross(right - left, down - up));
    gl_Position = ubo.viewProj * vec4(p, 1.0);
}
