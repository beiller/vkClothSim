#version 450
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in float aIdx;

layout(binding=0) uniform UBO { mat4 viewProj; } ubo;

struct Capsule { vec4 centerRadius; vec4 quat; vec4 halfLen; };
layout(binding=1) buffer Capsules { Capsule cs[]; } cb;

layout(location=0) out vec3 vN;
layout(location=1) out vec3 vP;
layout(location=2) out float vGround;

mat3 quatMat(vec4 q) {
    q = normalize(q);
    float x = q.x, y = q.y, z = q.z, w = q.w;
    return mat3(
        vec3(1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y + z * w),   2.0 * (x * z - y * w)),
        vec3(2.0 * (x * y - z * w),       1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z + x * w)),
        vec3(2.0 * (x * z + y * w),       2.0 * (y * z - x * w),   1.0 - 2.0 * (x * x + y * y)));
}

void main() {
    int i = int(aIdx);
    vec3 wp, wn;
    if (i < 0) {
        wp = aPos;
        wn = vec3(0.0, 1.0, 0.0);
        vGround = 1.0;
    } else {
        mat3 R = quatMat(cb.cs[i].quat);
        vec3 c = cb.cs[i].centerRadius.xyz;
        wp = R * aPos + c;
        wn = R * aNrm;
        vGround = 0.0;
    }
    vP = wp;
    vN = wn;
    gl_Position = ubo.viewProj * vec4(wp, 1.0);
}
