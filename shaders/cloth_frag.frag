#version 450
layout(location=0) out vec4 outColor;
layout(location=0) in vec3 vN;

void main() {
    vec3 n = normalize(vN);
    vec3 L = normalize(vec3(0.4, 0.8, 0.3));
    float d = abs(dot(n, L));
    vec3 base = vec3(0.25, 0.45, 0.78);
    vec3 col = base * (0.3 + 0.75 * d) + 0.06;
    outColor = vec4(col, 1.0);
}
