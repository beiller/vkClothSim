#version 450

layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D hdr;
layout(binding = 1) uniform TBO { vec4 region; float exposure; float lsb; } tbo;

vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 toSrgb(vec3 c) {
    return mix(12.92 * c, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

void main() {
    vec2 uv = (gl_FragCoord.xy - tbo.region.xy) / tbo.region.zw;
    vec3 s = toSrgb(aces(texture(hdr, uv).rgb * tbo.exposure));
    // ±half-LSB per-pixel dither: breaks up quantization banding in smooth gradients (8-bit output,
    // and 10-bit darks). Independent hash per channel so the steps don't align.
    vec2 p = gl_FragCoord.xy;
    s += (vec3(fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453),
               fract(sin(dot(p, vec2(93.9898, 67.345))) * 24634.6345),
               fract(sin(dot(p, vec2(57.1898, 12.987))) * 63458.5453)) - 0.5) * tbo.lsb;
    outColor = vec4(clamp(s, 0.0, 1.0), 1.0);
}
