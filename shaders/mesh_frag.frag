#version 450
// The ONE mesh fragment shader. Lights the per-vertex normal with the per-vertex color as the
// albedo (the ground/capsules/cloth/ball all share this material model; only the color
// differs). `abs` makes the lighting correct regardless of normal orientation (double-sided).
layout(location = 0) in vec3 vN;
layout(location = 1) in vec3 vCol;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 n = normalize(vN);
    vec3 L = normalize(vec3(0.4, 0.8, 0.3));
    float d = abs(dot(n, L));
    vec3 col = vCol * (0.28 + 0.9 * d) + 0.05;
    outColor = vec4(col, 1.0);
}
