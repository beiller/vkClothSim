// sim.hpp
// The soft-body simulator. Deliberately decoupled from the renderer (Vulkan) and the
// rigid-body physics (Jolt): it operates only on plain vertices + constraints + colliders.
//
//   - SoftBody       : a generic position-based (Verlet + sequential Gauss-Seidel) solver.
//                      It integrates a vertex list and solves two-point distance
//                      constraints. It knows nothing about meshes, triangles, pressure,
//                      capsules, or the ground.
//   - makeCloth/Ball : build a body's vertices + distance constraints (a grid / a sphere).
//   - applyCollision : one-way push-out of a body's vertices against capsule colliders +
//                      the ground (y=0). Colliders are plain segments — the caller extracts
//                      them from the rigid-body system.
//   - applyPressure  : a PBD volume constraint (expand/deflate to a target volume). Uses
//                      the body's triangles (which the solver itself never sees).
//
// The same SoftBody governs every soft body in the scene (cloth, ball, ...). A body is just
// (n vertices, a set of distance constraints, and a per-body parameter set).

#pragma once
#include <vector>
#include <cmath>

namespace sim {

// A two-point distance constraint: keep vertices a..b at rest length `rest`.
// `k` (0..1) scales how hard the constraint is enforced per relaxation (1 = stiff).
struct Constraint {
    int a, b;
    float rest;
    float k;
};

// A triangle (for the pressure + for rendering). The solver never uses it.
struct Triangle {
    int a, b, c;
};

// A capsule collider: segment [a,b] + radius. The collision pushes vertices out of it.
struct Collider {
    float a[3];
    float b[3];
    float r;
};

// Generic Verlet + sequential Gauss-Seidel solver. Vertices are stored as 3 floats each
// (x,y,z); `prev` holds the previous step's positions for Verlet integration.
class SoftBody {
public:
    int n = 0;
    int substeps = 1;
    int iterations = 4;
    float dt = 1.0f / 60.0f;
    float damping = 0.999f;
    float gravity[3] = {0, -9.81f, 0};   // acceleration applied during integration

    std::vector<float> pos;              // 3n
    std::vector<float> prev;             // 3n
    std::vector<Constraint> cons;

    // Build the body from an initial vertex list (3 floats each). prev starts equal to
    // pos (zero initial velocity).
    void init(const float* p0, int n, std::vector<Constraint> cons, float dt,
              const float* gravity, int substeps, int iterations, float damping) {
        this->n = n;
        this->dt = dt;
        this->substeps = substeps;
        this->iterations = iterations;
        this->damping = damping;
        for (int i = 0; i < 3; ++i) this->gravity[i] = gravity[i];
        pos.assign(p0, p0 + 3 * n);
        prev = pos;
        this->cons = std::move(cons);
    }

    // Restore an initial vertex list (used for reset).
    void reset(const float* p0) {
        pos.assign(p0, p0 + 3 * n);
        prev = pos;
    }

    // Integrate (Verlet + gravity) then solve the constraints (Gauss-Seidel), `substeps`
    // times. The caller applies pressure/collisions between/after steps.
    void step() {
        const float dt2 = dt * dt;
        for (int s = 0; s < substeps; ++s) {
            for (int i = 0; i < n; ++i) {
                float* p = &pos[3 * i], *q = &prev[3 * i];
                float vx = (p[0] - q[0]) * damping, vy = (p[1] - q[1]) * damping, vz = (p[2] - q[2]) * damping;
                q[0] = p[0]; q[1] = p[1]; q[2] = p[2];
                p[0] += vx + gravity[0] * dt2;
                p[1] += vy + gravity[1] * dt2;
                p[2] += vz + gravity[2] * dt2;
            }
            for (int it = 0; it < iterations; ++it)
                for (auto& c : cons) {
                    float* pa = &pos[3 * c.a], *pb = &pos[3 * c.b];
                    float dx = pb[0] - pa[0], dy = pb[1] - pa[1], dz = pb[2] - pa[2];
                    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (dist < 1e-6f) continue;
                    float k = (c.rest - dist) / dist * 0.5f * c.k;
                    float cx = dx * k, cy = dy * k, cz = dz * k;
                    pa[0] -= cx; pa[1] -= cy; pa[2] -= cz;
                    pb[0] += cx; pb[1] += cy; pb[2] += cz;
                }
        }
    }

    int numVertices() const { return n; }
    float* posPtr(int i) { return &pos[3 * i]; }
    const float* posPtr(int i) const { return &pos[3 * i]; }
    float* prevPtr(int i) { return &prev[3 * i]; }
};

// One-way collision: push every vertex of a soft body out of the capsule colliders + the
// ground (y=0), removing the normal velocity component (prev is adjusted). Generic over
// the body: the same routine works for the cloth, the ball, and any other soft body.
inline void applyCollision(SoftBody& body, const std::vector<Collider>& colliders) {
    const int n = body.numVertices();
    for (int i = 0; i < n; ++i) {
        float* p = body.posPtr(i);
        float* q = body.prevPtr(i);
        for (auto& c : colliders) {
            float abx = c.b[0] - c.a[0], aby = c.b[1] - c.a[1], abz = c.b[2] - c.a[2];
            float t = ((p[0] - c.a[0]) * abx + (p[1] - c.a[1]) * aby + (p[2] - c.a[2]) * abz) /
                      std::fmax(abx * abx + aby * aby + abz * abz, 1e-6f);
            t = std::fmax(0.0f, std::fmin(1.0f, t));
            float qx = c.a[0] + abx * t, qy = c.a[1] + aby * t, qz = c.a[2] + abz * t;
            float dx = p[0] - qx, dy = p[1] - qy, dz = p[2] - qz;
            float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist < c.r) {
                float inv = dist > 1e-5f ? 1.0f / dist : 0.0f;
                float nx = dx * inv, ny = (dist > 1e-5f ? dy * inv : 1.0f), nz = dz * inv;
                float tx = qx + nx * (c.r + 0.01f), ty = qy + ny * (c.r + 0.01f), tz = qz + nz * (c.r + 0.01f);
                float vx = p[0] - q[0], vy = p[1] - q[1], vz = p[2] - q[2];
                float vn = vx * nx + vy * ny + vz * nz;
                p[0] = tx; p[1] = ty; p[2] = tz;
                q[0] = tx - (vx - vn * nx); q[1] = ty - (vy - vn * ny); q[2] = tz - (vz - vn * nz);
            }
        }
        if (p[1] < 0.0f) {   // ground plane y=0
            float vx = p[0] - q[0], vz = p[2] - q[2];
            p[1] = 0.0f;
            q[0] = p[0] - vx; q[1] = p[1]; q[2] = p[2] - vz;
        }
    }
}

// Signed volume of a closed triangle mesh (tetrahedron sum).
inline float bodyVolume(const SoftBody& body, const std::vector<Triangle>& tris) {
    float v = 0.0f;
    for (auto& t : tris) {
        const float* a = body.posPtr(t.a), *b = body.posPtr(t.b), *c = body.posPtr(t.c);
        v += a[0] * (b[1] * c[2] - b[2] * c[1]) + a[1] * (b[2] * c[0] - b[0] * c[2]) + a[2] * (b[0] * c[1] - b[1] * c[0]);
    }
    return v / 6.0f;
}

// PBD volume constraint (the "pressure"): expand/deflate a body to a target volume via a
// per-vertex gradient step on the signed volume.
inline void applyPressure(SoftBody& body, const std::vector<Triangle>& tris, float vTarget) {
    const int n = body.numVertices();
    float C = bodyVolume(body, tris) - vTarget;
    std::vector<float> grad(3 * n, 0.0f);
    for (auto& t : tris) {
        const float* pa = body.posPtr(t.a), *pb = body.posPtr(t.b), *pc = body.posPtr(t.c);
        float e1x = pb[0] - pa[0], e1y = pb[1] - pa[1], e1z = pb[2] - pa[2];
        float e2x = pc[0] - pa[0], e2y = pc[1] - pa[1], e2z = pc[2] - pa[2];
        float nx = e1y * e2z - e1z * e2y, ny = e1z * e2x - e1x * e2z, nz = e1x * e2y - e1y * e2x;
        grad[3 * t.a + 0] += nx / 3; grad[3 * t.a + 1] += ny / 3; grad[3 * t.a + 2] += nz / 3;
        grad[3 * t.b + 0] += nx / 3; grad[3 * t.b + 1] += ny / 3; grad[3 * t.b + 2] += nz / 3;
        grad[3 * t.c + 0] += nx / 3; grad[3 * t.c + 1] += ny / 3; grad[3 * t.c + 2] += nz / 3;
    }
    float denom = 0.0f;
    for (int i = 0; i < n; ++i) {
        float gx = grad[3 * i], gy = grad[3 * i + 1], gz = grad[3 * i + 2];
        denom += gx * gx + gy * gy + gz * gz;
    }
    if (denom > 1e-12f) {
        float alpha = -C / denom;
        for (int i = 0; i < n; ++i) {
            float* p = body.posPtr(i);
            p[0] += alpha * grad[3 * i]; p[1] += alpha * grad[3 * i + 1]; p[2] += alpha * grad[3 * i + 2];
        }
    }
}

// Build a CW x CH grid of vertices (3 floats each) + the distance constraints (the grid
// edges + diagonals) + the base rest lengths (tension = 1.0). The caller scales the rest
// lengths by the tension each step.
inline void makeCloth(std::vector<float>& verts, std::vector<Constraint>& cons,
                      std::vector<float>& baseRest, int CW, int CH, float span, float y0) {
    const int CN = CW * CH;
    verts.assign(3 * CN, 0.0f);
    for (int gy = 0; gy < CH; ++gy)
        for (int gx = 0; gx < CW; ++gx) {
            int i = gy * CW + gx;
            verts[3 * i + 0] = -span / 2 + span * gx / (CW - 1);
            verts[3 * i + 1] = y0;
            verts[3 * i + 2] = -span / 2 + span * gy / (CH - 1);
        }
    const float spacing = span / (CW - 1);
    const float diag = spacing * 1.41421356f;
    cons.clear();
    baseRest.clear();
    for (int i = 0; i < CN; ++i) {
        int gx = i % CW, gy = i / CW;
        if (gx < CW - 1) { cons.push_back({i, i + 1, spacing, 1.0f}); baseRest.push_back(spacing); }
        if (gy < CH - 1) { cons.push_back({i, i + CW, spacing, 1.0f}); baseRest.push_back(spacing); }
        if (gx < CW - 1 && gy < CH - 1) { cons.push_back({i, i + CW + 1, diag, 1.0f}); baseRest.push_back(diag); }
        if (gx > 0 && gy < CH - 1) { cons.push_back({i, i + CW - 1, diag, 1.0f}); baseRest.push_back(diag); }
    }
}

// Build a UV-sphere: the vertex list (3 floats/vert), the distance constraints (the
// sphere's edges, soft k=0.5), and the triangles (for the pressure + render). The two
// poles are single vertices; the interior is (lat-1) rings of `lon` vertices each.
inline void makeBall(std::vector<float>& verts, std::vector<Constraint>& cons,
                     std::vector<Triangle>& tris, int lat, int lon, float radius, float y0) {
    auto vid = [lat, lon](int r, int c) {
        if (r == 0) return 0;
        if (r == lat) return 1 + (lat - 1) * lon;
        return 1 + (r - 1) * lon + (c % lon);
    };
    auto vpos = [lat, lon, radius](int r, int c, float& x, float& y, float& z) {
        if (r == 0) { x = 0; y = radius; z = 0; return; }
        if (r == lat) { x = 0; y = -radius; z = 0; return; }
        float phi = 3.14159265f * r / lat, theta = 2.0f * 3.14159265f * c / lon;
        x = radius * std::sin(phi) * std::cos(theta);
        y = radius * std::cos(phi);
        z = radius * std::sin(phi) * std::sin(theta);
    };
    const int BN = 2 + (lat - 1) * lon;
    verts.assign(3 * BN, 0.0f);
    for (int r = 0; r <= lat; ++r)
        for (int c = 0; c < lon; ++c) {
            int i = vid(r, c); float x, y, z; vpos(r, c, x, y, z);
            verts[3 * i + 0] = x; verts[3 * i + 1] = y0 + y; verts[3 * i + 2] = z;
        }
    auto bdist = [&](int r1, int c1, int r2, int c2) {
        float ax, ay, az, bx, by, bz; vpos(r1, c1, ax, ay, az); vpos(r2, c2, bx, by, bz);
        float dx = ax - bx, dy = ay - by, dz = az - bz;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    };
    cons.clear();
    tris.clear();
    for (int r = 0; r < lat; ++r)
        for (int c = 0; c < lon; ++c) {
            int a = vid(r, c), b = vid(r, (c + 1) % lon), c2 = vid(r + 1, c), d = vid(r + 1, (c + 1) % lon);
            cons.push_back({a, b, bdist(r, c, r, (c + 1) % lon), 0.5f});
            cons.push_back({a, c2, bdist(r, c, r + 1, c), 0.5f});
            cons.push_back({a, d, bdist(r, c, r + 1, (c + 1) % lon), 0.5f});
            tris.push_back({a, b, d});
            tris.push_back({a, d, c2});
        }
}

}  // namespace sim
