#version 450
layout(location=0) out vec4 outColor;
layout(location=0) in vec3 vN;
layout(location=1) in vec3 vP;
layout(location=2) in float vGround;

void main() {
    vec3 n = normalize(vN);
    vec3 L = normalize(vec3(0.4, 0.8, 0.3));
    float d = max(dot(n, L), 0.0);
    vec3 base = vGround > 0.5 ? vec3(0.19, 0.21, 0.17) : vec3(0.85, 0.35, 0.30);
    vec3 col = base * (0.28 + 0.9 * d) + 0.05;
    outColor = vec4(col, 1.0);
}
