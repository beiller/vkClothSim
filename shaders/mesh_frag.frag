#version 450

layout(location = 0) in vec3 vN;
layout(location = 1) in vec3 vCol;
layout(location = 2) in vec2 vUv;
layout(location = 0) out vec4 outColor;

float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

vec2 grad2(float a) {
    return vec2(cos(a), sin(a));
}

float perlin(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    vec2 P = vec2(8.0);
    float g00 = dot(grad2(6.2831853 * hash21(mod(i, P))), f);
    float g10 = dot(grad2(6.2831853 * hash21(mod(i + vec2(1.0, 0.0), P))), f - vec2(1.0, 0.0));
    float g01 = dot(grad2(6.2831853 * hash21(mod(i + vec2(0.0, 1.0), P))), f - vec2(0.0, 1.0));
    float g11 = dot(grad2(6.2831853 * hash21(mod(i + vec2(1.0, 1.0), P))), f - vec2(1.0, 1.0));
    return mix(mix(g00, g10, u.x), mix(g01, g11, u.x), u.y) * 0.5 + 0.5;
}

void main() {
    vec3 n = normalize(vN);
    vec3 L = normalize(vec3(0.4, 0.8, 0.3));
    float d = abs(dot(n, L));
    float nz = perlin(vUv * 8.0);
    vec3 col = vCol * (0.28 + 0.9 * d) * (0.55 + 0.9 * nz) + 0.05;
    outColor = vec4(col, 1.0);
}
