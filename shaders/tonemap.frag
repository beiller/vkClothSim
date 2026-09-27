#version 450

layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D hdr;
layout(binding = 1) uniform TBO { vec2 extent; float exposure; } tbo;

vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 toSrgb(vec3 c) {
    return mix(12.92 * c, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

void main() {
    vec2 uv = gl_FragCoord.xy / tbo.extent;
    vec3 c = aces(texture(hdr, uv).rgb * tbo.exposure);
    outColor = vec4(toSrgb(c), 1.0);
}
