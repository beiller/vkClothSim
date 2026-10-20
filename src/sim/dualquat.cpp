#include "sim/dualquat.hpp"

#include <cmath>
#include <cstring>

namespace dq {
namespace {

float square_f(float x) { return x * x; }
float len_v3(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
float len_squared_v3(const float v[3]) { return v[0] * v[0] + v[1] * v[1] + v[2] * v[2]; }
float dot_v3v3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

void copy_m4_m4(float m1[4][4], const float m2[4][4]) { std::memcpy(m1, m2, sizeof(float[4][4])); }
void copy_m3_m3(float m1[3][3], const float m2[3][3]) { std::memcpy(m1, m2, sizeof(float[3][3])); }
void copy_v3_v3(float m1[3], const float m2[3]) { std::memcpy(m1, m2, sizeof(float[3])); }
void copy_m3_m4(float m1[3][3], const float m2[4][4]) {
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r)
            m1[c][r] = m2[c][r];
}
void sub_v3_v3(float r[3], const float a[3]) { // in-place: r -= a
    r[0] -= a[0];
    r[1] -= a[1];
    r[2] -= a[2];
}
void cross_v3_v3v3(float r[3], const float a[3], const float b[3]) {
    r[0] = a[1] * b[2] - a[2] * b[1];
    r[1] = a[2] * b[0] - a[0] * b[2];
    r[2] = a[0] * b[1] - a[1] * b[0];
}
void mul_v3_fl(float v[3], float f) {
    v[0] *= f;
    v[1] *= f;
    v[2] *= f;
}
void normalize_v3(float v[3]) {
    const float l = len_v3(v);
    if (l > 0.0f) {
        const float s = 1.0f / l;
        mul_v3_fl(v, s);
    }
}

float dot_qtqt(const float a[4], const float b[4]);
void mul_qt_fl(float q[4], float f);
void copy_qt_qt(float d[4], const float s[4]);
void normalize_qt(float q[4]);

void mul_m4_m4m4(float R[4][4], const float A[4][4], const float B[4][4]) {
    if (R == A || R == B) {
        float T[4][4];
        mul_m4_m4m4(T, A, B);
        copy_m4_m4(R, T);
        return;
    }
    for (int j = 0; j < 4; ++j)
        for (int k = 0; k < 4; ++k) {
            float s = 0.0f;
            for (int i = 0; i < 4; ++i)
                s += B[j][i] * A[i][k];
            R[j][k] = s;
        }
}

void mul_m3_m3m3(float R[3][3], const float A[3][3], const float B[3][3]) {
    if (R == A || R == B) {
        float T[3][3];
        mul_m3_m3m3(T, A, B);
        copy_m3_m3(R, T);
        return;
    }
    for (int j = 0; j < 3; ++j)
        for (int k = 0; k < 3; ++k) {
            float s = 0.0f;
            for (int i = 0; i < 3; ++i)
                s += B[j][i] * A[i][k];
            R[j][k] = s;
        }
}

void mul_m4_v3(const float M[4][4], float r[3]) {
    const float x = r[0], y = r[1];
    r[0] = x * M[0][0] + y * M[1][0] + M[2][0] * r[2] + M[3][0];
    r[1] = x * M[0][1] + y * M[1][1] + M[2][1] * r[2] + M[3][1];
    r[2] = x * M[0][2] + y * M[1][2] + M[2][2] * r[2] + M[3][2];
}

void mul_v3_m4v3(float r[3], const float mat[4][4], const float vec[3]) {
    const float x = vec[0], y = vec[1];
    r[0] = x * mat[0][0] + y * mat[1][0] + mat[2][0] * vec[2] + mat[3][0];
    r[1] = x * mat[0][1] + y * mat[1][1] + mat[2][1] * vec[2] + mat[3][1];
    r[2] = x * mat[0][2] + y * mat[1][2] + mat[2][2] * vec[2] + mat[3][2];
}

void mul_m3_v3(const float M[3][3], float r[3]) {
    const float x = r[0], y = r[1];
    r[0] = x * M[0][0] + y * M[1][0] + M[2][0] * r[2];
    r[1] = x * M[0][1] + y * M[1][1] + M[2][1] * r[2];
    r[2] = x * M[0][2] + y * M[1][2] + M[2][2] * r[2];
}

void mul_m4_fl(float R[4][4], float f) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            R[c][r] *= f;
}
void mul_m3_fl(float R[3][3], float f) {
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r)
            R[c][r] *= f;
}
void add_m4_m4m4(float R[4][4], const float A[4][4], const float B[4][4]) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            R[c][r] = A[c][r] + B[c][r];
}
void mul_m4_series(float R[4][4], const float A[4][4], const float B[4][4], const float C[4][4]) {
    float T[4][4];
    mul_m4_m4m4(T, A, B);
    mul_m4_m4m4(R, T, C);
}

void mat4_to_size(float size[3], const float M[4][4]) {
    size[0] = len_v3(M[0]);
    size[1] = len_v3(M[1]);
    size[2] = len_v3(M[2]);
}

float determinant_m3(const float m[3][3]) {
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
           m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

float determinant_m4(const float m[4][4]) {
    const float a1 = m[0][0], b1 = m[0][1], c1 = m[0][2], d1 = m[0][3];
    const float a2 = m[1][0], b2 = m[1][1], c2 = m[1][2], d2 = m[1][3];
    const float a3 = m[2][0], b3 = m[2][1], c3 = m[2][2], d3 = m[2][3];
    const float a4 = m[3][0], b4 = m[3][1], c4 = m[3][2], d4 = m[3][3];
    auto m3 = [](float a0, float a1, float a2, float a3, float a4, float a5, float a6, float a7, float a8) {
        return a0 * (a4 * a8 - a5 * a7) - a3 * (a1 * a8 - a5 * a0) + a6 * (a1 * a7 - a4 * a0);
    };
    return a1 * m3(b2, b3, b4, c2, c3, c4, d2, d3, d4) - b1 * m3(a2, a3, a4, c2, c3, c4, d2, d3, d4) +
           c1 * m3(a2, a3, a4, b2, b3, b4, d2, d3, d4) - d1 * m3(a2, a3, a4, b2, b3, b4, c2, c3, c4);
}

bool is_orthonormal_m3(const float m[3][3]) {
    for (int i = 0; i < 3; ++i)
        for (int j = i; j < 3; ++j) {
            const float expect = (i == j) ? 1.0f : 0.0f;
            if (std::fabs(dot_v3v3(m[i], m[j]) - expect) > 1e-5f)
                return false;
        }
    return true;
}

void negate_m3(float m[3][3]) { mul_m3_fl(m, -1.0f); }
void unit_m3(float m[3][3]) {
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r)
            m[c][r] = (c == r) ? 1.0f : 0.0f;
}

void orthogonalize_m4(float R[4][4], int axis) {
    float size[3];
    mat4_to_size(size, R);
    normalize_v3(R[axis]);
    switch (axis) {
        case 0:
            if (dot_v3v3(R[0], R[1]) < 1) {
                cross_v3_v3v3(R[2], R[0], R[1]);
                normalize_v3(R[2]);
                cross_v3_v3v3(R[1], R[2], R[0]);
            } else if (dot_v3v3(R[0], R[2]) < 1) {
                cross_v3_v3v3(R[1], R[2], R[0]);
                normalize_v3(R[1]);
                cross_v3_v3v3(R[2], R[0], R[1]);
            } else {
                float vec[3] = {R[0][1], R[0][2], R[0][0]};
                cross_v3_v3v3(R[2], R[0], vec);
                normalize_v3(R[2]);
                cross_v3_v3v3(R[1], R[2], R[0]);
            }
            break;
        case 1:
            if (dot_v3v3(R[1], R[0]) < 1) {
                cross_v3_v3v3(R[2], R[0], R[1]);
                normalize_v3(R[2]);
                cross_v3_v3v3(R[0], R[1], R[2]);
            } else if (dot_v3v3(R[0], R[2]) < 1) {
                cross_v3_v3v3(R[1], R[0], R[2]);
                normalize_v3(R[1]);
                cross_v3_v3v3(R[2], R[1], R[0]);
            } else {
                float vec[3] = {R[1][1], R[1][2], R[1][0]};
                cross_v3_v3v3(R[0], R[1], vec);
                normalize_v3(R[0]);
                cross_v3_v3v3(R[2], R[0], R[1]);
            }
            break;
        case 2:
        default:
            if (dot_v3v3(R[2], R[0]) < 1) {
                cross_v3_v3v3(R[1], R[2], R[0]);
                normalize_v3(R[1]);
                cross_v3_v3v3(R[0], R[1], R[2]);
            } else if (dot_v3v3(R[2], R[1]) < 1) {
                cross_v3_v3v3(R[0], R[1], R[2]);
                normalize_v3(R[0]);
                cross_v3_v3v3(R[1], R[2], R[0]);
            } else {
                float vec[3] = {R[2][1], R[2][2], R[2][0]};
                cross_v3_v3v3(R[0], vec, R[2]);
                normalize_v3(R[0]);
                cross_v3_v3v3(R[1], R[2], R[0]);
            }
            break;
    }
    mul_v3_fl(R[0], size[0]);
    mul_v3_fl(R[1], size[1]);
    mul_v3_fl(R[2], size[2]);
}

void invert_m4_m4(float inv[4][4], const float M[4][4]) {
    for (int c = 0; c < 4; ++c)
        for (int rrow = 0; rrow < 4; ++rrow)
            inv[c][rrow] = 0.0f;
    float r[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r[i][j] = M[j][i];
    const float a = r[0][0], b = r[0][1], c = r[0][2], d = r[1][0], e = r[1][1], f = r[1][2];
    const float g = r[2][0], h = r[2][1], k = r[2][2];

    float det = a * (e * k - f * h) - b * (d * k - f * g) + c * (d * h - e * g);
    if (det == 0.0f)
        det = 1.0f; // degenerate: avoid divide-by-zero
    const float idet = 1.0f / det;

    float invr[3][3];
    invr[0][0] = (e * k - f * h) * idet;
    invr[0][1] = (c * h - b * k) * idet;
    invr[0][2] = (b * f - c * e) * idet;
    invr[1][0] = (f * g - d * k) * idet;
    invr[1][1] = (a * k - c * g) * idet;
    invr[1][2] = (c * d - a * f) * idet;
    invr[2][0] = (d * h - e * g) * idet;
    invr[2][1] = (g * b - a * h) * idet;
    invr[2][2] = (a * e - b * d) * idet;

    const float tx = M[3][0], ty = M[3][1], tz = M[3][2];
    const float p0 = invr[0][0] * tx + invr[0][1] * ty + invr[0][2] * tz;
    const float p1 = invr[1][0] * tx + invr[1][1] * ty + invr[1][2] * tz;
    const float p2 = invr[2][0] * tx + invr[2][1] * ty + invr[2][2] * tz;

    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            inv[j][i] = invr[i][j]; // back to column-major
    inv[3][0] = -p0;
    inv[3][1] = -p1;
    inv[3][2] = -p2;
    inv[3][3] = 1.0f;
}

void mat3_normalized_to_quat_fast(float q[4], const float mat[3][3]) {
    if (mat[2][2] < 0.0f) {
        if (mat[0][0] > mat[1][1]) {
            const float trace = 1.0f + mat[0][0] - mat[1][1] - mat[2][2];
            float s = 2.0f * std::sqrt(trace);
            if (mat[1][2] < mat[2][1])
                s = -s;
            q[1] = 0.25f * s;
            s = 1.0f / s;
            q[0] = (mat[1][2] - mat[2][1]) * s;
            q[2] = (mat[0][1] + mat[1][0]) * s;
            q[3] = (mat[2][0] + mat[0][2]) * s;
            if (trace == 1.0f && (q[0] == 0.0f && q[2] == 0.0f && q[3] == 0.0f))
                q[1] = 1.0f;
        } else {
            const float trace = 1.0f - mat[0][0] + mat[1][1] - mat[2][2];
            float s = 2.0f * std::sqrt(trace);
            if (mat[2][0] < mat[0][2])
                s = -s;
            q[2] = 0.25f * s;
            s = 1.0f / s;
            q[0] = (mat[2][0] - mat[0][2]) * s;
            q[1] = (mat[0][1] + mat[1][0]) * s;
            q[3] = (mat[1][2] + mat[2][1]) * s;
            if (trace == 1.0f && (q[0] == 0.0f && q[1] == 0.0f && q[3] == 0.0f))
                q[2] = 1.0f;
        }
    } else {
        if (mat[0][0] < -mat[1][1]) {
            const float trace = 1.0f - mat[0][0] - mat[1][1] + mat[2][2];
            float s = 2.0f * std::sqrt(trace);
            if (mat[0][1] < mat[1][0])
                s = -s;
            q[3] = 0.25f * s;
            s = 1.0f / s;
            q[0] = (mat[0][1] - mat[1][0]) * s;
            q[1] = (mat[2][0] + mat[0][2]) * s;
            q[2] = (mat[1][2] + mat[2][1]) * s;
            if (trace == 1.0f && (q[0] == 0.0f && q[1] == 0.0f && q[2] == 0.0f))
                q[3] = 1.0f;
        } else {
            const float trace = 1.0f + mat[0][0] + mat[1][1] + mat[2][2];
            float s = 2.0f * std::sqrt(trace);
            q[0] = 0.25f * s;
            s = 1.0f / s;
            q[1] = (mat[1][2] - mat[2][1]) * s;
            q[2] = (mat[2][0] - mat[0][2]) * s;
            q[3] = (mat[0][1] - mat[1][0]) * s;
            if (trace == 1.0f && (q[1] == 0.0f && q[2] == 0.0f && q[3] == 0.0f))
                q[0] = 1.0f;
        }
    }
    const float q_len_squared = dot_qtqt(q, q);
    if (std::fabs(q_len_squared - 1.0f) >= 0.0006f)
        normalize_qt(q);
}

void normalize_m3(float R[3][3]) {
    for (int c = 0; c < 3; ++c)
        normalize_v3(R[c]);
}

void mat3_normalized_to_quat_with_checks(float q[4], float m3[3][3]) {
    const float det = determinant_m3(m3);
    if (!std::isfinite(det))
        unit_m3(m3);
    else if (det < 0.0f)
        negate_m3(m3);
    mat3_normalized_to_quat_fast(q, m3);
}

void mat4_to_quat(float q[4], const float mat[4][4]) {
    float m3[3][3];
    copy_m3_m4(m3, mat);
    normalize_m3(m3);
    mat3_normalized_to_quat_with_checks(q, m3);
}

void quat_to_mat4(float m[4][4], const float q[4]) {
    const double q0 = std::sqrt(2) * (double)q[0], q1 = std::sqrt(2) * (double)q[1];
    const double q2 = std::sqrt(2) * (double)q[2], q3 = std::sqrt(2) * (double)q[3];
    const double qda = q0 * q1, qdb = q0 * q2, qdc = q0 * q3;
    const double qaa = q1 * q1, qab = q1 * q2, qac = q1 * q3;
    const double qbb = q2 * q2, qbc = q2 * q3, qcc = q3 * q3;
    m[0][0] = (float)(1.0 - qbb - qcc);
    m[0][1] = (float)(qdc + qab);
    m[0][2] = (float)(-qdb + qac);
    m[0][3] = 0.0f;
    m[1][0] = (float)(-qdc + qab);
    m[1][1] = (float)(1.0 - qaa - qcc);
    m[1][2] = (float)(qda + qbc);
    m[1][3] = 0.0f;
    m[2][0] = (float)(qdb + qac);
    m[2][1] = (float)(-qda + qbc);
    m[2][2] = (float)(1.0 - qaa - qbb);
    m[2][3] = 0.0f;
    m[3][0] = 0.0f;
    m[3][1] = 0.0f;
    m[3][2] = 0.0f;
    m[3][3] = 1.0f;
}

void mul_qt_fl(float q[4], float f) {
    q[0] *= f;
    q[1] *= f;
    q[2] *= f;
    q[3] *= f;
}
float dot_qtqt(const float a[4], const float b[4]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}
void copy_qt_qt(float d[4], const float s[4]) { std::memcpy(d, s, 16); }
void normalize_qt(float q[4]) {
    const float l = std::sqrt(dot_qtqt(q, q));
    if (l > 0.0f)
        mul_qt_fl(q, 1.0f / l);
}

}

void mat4ToDquat(DualQuat* dq, const float basemat[4][4], const float mat[4][4]) {
    float dscale[3], scale[3], basequat[4], mat3[3][3];
    float baseRS[4][4], baseinv[4][4], baseR[4][4], baseRinv[4][4];
    float R[4][4], S[4][4];

    std::memset(dq->scale, 0, sizeof(dq->scale));
    std::memset(dq->trans, 0, sizeof(dq->trans));

    mul_m4_m4m4(baseRS, mat, basemat);
    mat4_to_size(scale, baseRS);
    dscale[0] = scale[0] - 1.0f;
    dscale[1] = scale[1] - 1.0f;
    dscale[2] = scale[2] - 1.0f;

    copy_m3_m4(mat3, mat);

    if (!is_orthonormal_m3(mat3) || (determinant_m4(mat) < 0.0f) ||
        len_squared_v3(dscale) > square_f(1e-4f)) {
        float tmp[4][4];
        copy_m4_m4(tmp, baseRS);
        orthogonalize_m4(tmp, 1);
        mat4_to_quat(basequat, tmp);
        quat_to_mat4(baseR, basequat);
        copy_v3_v3(baseR[3], baseRS[3]);

        invert_m4_m4(baseinv, basemat);
        mul_m4_m4m4(R, baseR, baseinv);
        invert_m4_m4(baseRinv, baseR);
        mul_m4_m4m4(S, baseRinv, baseRS);

        mul_m4_series(dq->scale, basemat, S, baseinv);
        dq->scale_weight = 1.0f;
    } else {
        copy_m4_m4(R, mat);
        dq->scale_weight = 0.0f;
    }

    mat4_to_quat(dq->quat, R);

    const float* t = R[3];
    const float* q = dq->quat;
    dq->trans[0] = -0.5f * (t[0] * q[1] + t[1] * q[2] + t[2] * q[3]);
    dq->trans[1] = 0.5f * (t[0] * q[0] + t[1] * q[3] - t[2] * q[2]);
    dq->trans[2] = 0.5f * (-t[0] * q[3] + t[1] * q[0] + t[2] * q[1]);
    dq->trans[3] = 0.5f * (t[0] * q[2] - t[1] * q[1] + t[2] * q[0]);
}

void dquatToMat4(float R[4][4], const DualQuat* dq) {
    float len, q0[4];
    const float* t;

    copy_qt_qt(q0, dq->quat);
    len = std::sqrt(dot_qtqt(q0, q0));
    if (len != 0.0f)
        len = 1.0f / len;
    mul_qt_fl(q0, len);

    quat_to_mat4(R, q0);

    t = dq->trans;
    R[3][0] = 2.0f * (-t[0] * q0[1] + t[1] * q0[0] - t[2] * q0[3] + t[3] * q0[2]) * len;
    R[3][1] = 2.0f * (-t[0] * q0[2] + t[1] * q0[3] + t[2] * q0[0] - t[3] * q0[1]) * len;
    R[3][2] = 2.0f * (-t[0] * q0[3] - t[1] * q0[2] + t[2] * q0[1] + t[3] * q0[0]) * len;

    if (dq->scale_weight)
        mul_m4_m4m4(R, R, dq->scale);
}

void addWeightedDqDq(DualQuat* dq_sum, const DualQuat* dq, float weight) {
    bool flipped = false;

    if (dot_qtqt(dq->quat, dq_sum->quat) < 0) {
        flipped = true;
        weight = -weight;
    }

    dq_sum->quat[0] += weight * dq->quat[0];
    dq_sum->quat[1] += weight * dq->quat[1];
    dq_sum->quat[2] += weight * dq->quat[2];
    dq_sum->quat[3] += weight * dq->quat[3];

    dq_sum->trans[0] += weight * dq->trans[0];
    dq_sum->trans[1] += weight * dq->trans[1];
    dq_sum->trans[2] += weight * dq->trans[2];
    dq_sum->trans[3] += weight * dq->trans[3];

    if (dq->scale_weight) {
        float wmat[4][4];
        if (flipped)
            weight = -weight;
        copy_m4_m4(wmat, dq->scale);
        mul_m4_fl(wmat, weight);
        add_m4_m4m4(dq_sum->scale, dq_sum->scale, wmat);
        dq_sum->scale_weight += weight;
    }
}

void addWeightedDqDqPivot(DualQuat* dq_sum, const DualQuat* dq, const float pivot[3], float weight,
                          bool compute_scale_matrix) {
    if (dq->scale_weight) {
        DualQuat mdq = *dq;

        float dst[3];
        mul_v3_m4v3(dst, mdq.scale, pivot);
        sub_v3_v3(dst, pivot);

        mdq.trans[0] -= .5f * (mdq.quat[1] * dst[0] + mdq.quat[2] * dst[1] + mdq.quat[3] * dst[2]);
        mdq.trans[1] += .5f * (mdq.quat[0] * dst[0] + mdq.quat[2] * dst[2] - mdq.quat[3] * dst[1]);
        mdq.trans[2] += .5f * (mdq.quat[0] * dst[1] + mdq.quat[3] * dst[0] - mdq.quat[1] * dst[2]);
        mdq.trans[3] += .5f * (mdq.quat[0] * dst[2] + mdq.quat[1] * dst[1] - mdq.quat[2] * dst[0]);

        if (compute_scale_matrix) {
            sub_v3_v3(mdq.scale[3], dst);
        } else {
            mdq.scale_weight = 0.0f;
        }

        addWeightedDqDq(dq_sum, &mdq, weight);
    } else {
        addWeightedDqDq(dq_sum, dq, weight);
    }
}

void normalizeDq(DualQuat* dq, float totweight) {
    const float scale = 1.0f / totweight;
    mul_qt_fl(dq->quat, scale);
    mul_qt_fl(dq->trans, scale);

    if (dq->scale_weight) {
        const float addweight = totweight - dq->scale_weight;
        if (addweight) {
            dq->scale[0][0] += addweight;
            dq->scale[1][1] += addweight;
            dq->scale[2][2] += addweight;
            dq->scale[3][3] += addweight;
        }
        mul_m4_fl(dq->scale, scale);
        dq->scale_weight = 1.0f;
    }
}

void mulV3m3Dq(float r[3], float R[3][3], DualQuat* dq) {
    float M[3][3], t[3], scalemat[3][3], len2;
    const float w = dq->quat[0], x = dq->quat[1], y = dq->quat[2], z = dq->quat[3];
    const float t0 = dq->trans[0], t1 = dq->trans[1], t2 = dq->trans[2], t3 = dq->trans[3];

    M[0][0] = w * w + x * x - y * y - z * z;
    M[1][0] = 2 * (x * y - w * z);
    M[2][0] = 2 * (x * z + w * y);
    M[0][1] = 2 * (x * y + w * z);
    M[1][1] = w * w + y * y - x * x - z * z;
    M[2][1] = 2 * (y * z - w * x);
    M[0][2] = 2 * (x * z - w * y);
    M[1][2] = 2 * (y * z + w * x);
    M[2][2] = w * w + z * z - x * x - y * y;

    len2 = dot_qtqt(dq->quat, dq->quat);
    if (len2 > 0.0f)
        len2 = 1.0f / len2;

    t[0] = 2 * (-t0 * x + w * t1 - t2 * z + y * t3);
    t[1] = 2 * (-t0 * y + t1 * z - x * t3 + w * t2);
    t[2] = 2 * (-t0 * z + x * t2 + w * t3 - t1 * y);

    if (dq->scale_weight)
        mul_m4_v3(dq->scale, r);

    mul_m3_v3(M, r);
    r[0] = (r[0] + t[0]) * len2;
    r[1] = (r[1] + t[1]) * len2;
    r[2] = (r[2] + t[2]) * len2;

    if (R) {
        if (dq->scale_weight) {
            copy_m3_m4(scalemat, dq->scale);
            mul_m3_m3m3(R, M, scalemat);
        } else {
            copy_m3_m3(R, M);
        }
        mul_m3_fl(R, len2);
    }
}

}
