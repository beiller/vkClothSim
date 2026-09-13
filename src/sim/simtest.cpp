#include "sim/sim.hpp"

#include "app/params.hpp"
#include "app/rigid.hpp"
#include "math.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

namespace {

struct PhysParams {
    int nCaps;
    float dt, damping, gravity, friction, skin, fricMargin, tension, stiff, maxStep;
};

constexpr float COLLIDE_COMPLIANCE = 0.02f;
constexpr float FRICTION_COMPLIANCE = 0.0f;
constexpr float MIN_CONSTRAINT_ALPHA = 10.0f;
float constraintAlpha(const PhysParams& ph) {
    float a = (ph.stiff > 1e-3f) ? 10.0f / ph.stiff : 1e3f;
    return std::max(a, MIN_CONSTRAINT_ALPHA);
}

struct F3 {
    float x, y, z;
};
F3 opAdd(F3 a, F3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
F3 opSub(F3 a, F3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
F3 opScale(F3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}
float dot(F3 a, F3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
float len(F3 a) {
    return std::sqrt(dot(a, a));
}

void quatToMat3(const float q[4], float m[9]) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    m[0] = 1.0f - 2.0f * (y * y + z * z);
    m[1] = 2.0f * (x * y + z * w);
    m[2] = 2.0f * (x * z - y * w);
    m[3] = 2.0f * (x * y - z * w);
    m[4] = 1.0f - 2.0f * (x * x + z * z);
    m[5] = 2.0f * (y * z + x * w);
    m[6] = 2.0f * (x * z + y * w);
    m[7] = 2.0f * (y * z - x * w);
    m[8] = 1.0f - 2.0f * (x * x + y * y);
}

struct CapInfo {
    F3 A, B, AB;
    float ab2, radius;
};
std::vector<CapInfo> prepareCaps(const std::vector<CapsuleGPU>& caps) {
    std::vector<CapInfo> out(caps.size());
    for (size_t c = 0; c < caps.size(); ++c) {
        const CapsuleGPU& cg = caps[c];
        float m[9];
        quatToMat3(cg.quat, m);
        F3 axis = opScale({m[3], m[4], m[5]}, cg.halfLen[0]);
        F3 center = {cg.centerRadius[0], cg.centerRadius[1], cg.centerRadius[2]};
        CapInfo& ci = out[c];
        ci.A = opSub(center, axis);
        ci.B = opAdd(center, axis);
        ci.AB = opSub(ci.B, ci.A);
        ci.ab2 = dot(ci.AB, ci.AB);
        ci.radius = cg.centerRadius[3];
    }
    return out;
}

struct Body {
    int n = 0;
    std::vector<F3> pos, prev, sub0;
    struct Contact {
        F3 n;
        float lambda;
    };
    std::vector<Contact> contact;
    struct Entry {
        int j;
        float rest, k;
    };
    std::vector<Entry> ents;
    std::vector<int> estart, colorStart, colorVerts;
    int colorCount = 0;
    std::vector<F3> initPos;
    const std::vector<sim::Constraint>* cons = nullptr;

    void build(const sim::SoftBody& sb) {
        n = sb.numVertices();
        pos.resize(n);
        prev.resize(n);
        sub0.resize(n);
        contact.resize(n);
        initPos.resize(n);
        for (int i = 0; i < n; ++i) {
            const float* p = sb.posPtr(i);
            initPos[i] = {p[0], p[1], p[2]};
            pos[i] = initPos[i];
            prev[i] = initPos[i];
            sub0[i] = initPos[i];
            contact[i] = Contact{{0.0f, 0.0f, 0.0f}, 0.0f};
        }
        std::vector<std::vector<Entry>> lists(n);
        for (const auto& c : sb.cons) {
            lists[c.a].push_back({c.b, c.rest, c.k});
            lists[c.b].push_back({c.a, c.rest, c.k});
        }
        estart.assign(n + 1, 0);
        for (int i = 0; i < n; ++i)
            estart[i + 1] = estart[i] + (int)lists[i].size();
        ents.resize(estart[n]);
        for (int i = 0; i < n; ++i)
            for (size_t e = 0; e < lists[i].size(); ++e)
                ents[estart[i] + e] = lists[i][e];
        cons = &sb.cons;
        colorStart = sb.colorStart;
        colorVerts = sb.colorVerts;
        colorCount = sb.colorCount;
    }

    void reset() {
        pos = initPos;
        prev = initPos;
        sub0 = initPos;
        resetContacts();
    }

    void resetContacts() {
        for (int i = 0; i < n; ++i)
            contact[i] = Contact{{0.0f, 0.0f, 0.0f}, 0.0f};
    }

    void verlet(const PhysParams& ph) {
        for (int i = 0; i < n; ++i) {
            F3 P = pos[i], Pr = prev[i];
            F3 d = opScale(opSub(P, Pr), ph.damping);
            d.y += ph.gravity * ph.dt * ph.dt;
            float dl = len(d);
            if (dl > ph.maxStep)
                d = opScale(d, ph.maxStep / dl);
            pos[i] = opAdd(P, d);
        }
    }

    F3 collideVertex(const PhysParams& ph, int i, F3 P, const std::vector<CapInfo>& caps) {
        Contact ct = contact[i];
        for (int c = 0; c < ph.nCaps; ++c) {
            const CapInfo& ci = caps[c];
            float t = std::clamp(dot(opSub(P, ci.A), ci.AB) / std::max(ci.ab2, 1e-6f), 0.0f, 1.0f);
            F3 Q = opAdd(ci.A, opScale(ci.AB, t));
            F3 d = opSub(P, Q);
            float dist = len(d);
            float C = dist - (ci.radius + ph.skin);
            if (C < 0.0f) {
                F3 nrm = dist > 1e-5f ? opScale(d, 1.0f / dist) : F3{0, 1, 0};
                float corr = -C / (1.0f + COLLIDE_COMPLIANCE);
                P = opAdd(P, opScale(nrm, corr));
                ct.n = opAdd(ct.n, opScale(nrm, corr));
                ct.lambda += corr;
            }
        }
        if (P.y < 0.0f) {
            float corr = -P.y / (1.0f + COLLIDE_COMPLIANCE);
            P.y += corr;
            ct.n.y += corr;
            ct.lambda += corr;
        }
        float nl = len(ct.n);
        if (nl > 1e-8f) {
            F3 fn = opScale(ct.n, 1.0f / nl);
            F3 rel = opSub(P, sub0[i]);
            F3 vt = opSub(rel, opScale(fn, dot(rel, fn)));
            float l = len(vt);
            float limit = ph.friction * ct.lambda;
            if (l > 1e-8f && limit > 0.0f) {
                float corr = std::min(l, limit) / (1.0f + FRICTION_COMPLIANCE);
                P = opSub(P, opScale(vt, corr / l));
            }
        }
        contact[i] = ct;
        return P;
    }

    void relaxVertex(const PhysParams& ph, int i) {
        F3 P = pos[i];
        float alpha = constraintAlpha(ph);
        for (int e = estart[i]; e < estart[i + 1]; ++e) {
            const Entry& en = ents[e];
            F3 d = opSub(pos[en.j], P);
            float dist = len(d);
            if (dist < 1e-6f)
                continue;
            float rest = en.rest * ph.tension;
            P = opAdd(P, opScale(d, (dist - rest) / dist * (en.k / (2.0f + alpha))));
        }
        pos[i] = P;
    }

    void solveVertex(const PhysParams& ph, int i, const std::vector<CapInfo>& caps) {
        pos[i] = collideVertex(ph, i, pos[i], caps);
        relaxVertex(ph, i);
    }

    void collideAll(const PhysParams& ph, const std::vector<CapInfo>& caps) {
        for (int i = 0; i < n; ++i)
            pos[i] = collideVertex(ph, i, pos[i], caps);
    }

    void finalize(const PhysParams& ph) {
        for (int i = 0; i < n; ++i) {
            F3 P = pos[i];
            F3 v = opScale(opSub(P, sub0[i]), 1.0f / ph.dt);
            float nl = len(contact[i].n);
            if (nl > 1e-8f) {
                F3 fn = opScale(contact[i].n, 1.0f / nl);
                float vn = dot(v, fn);
                if (vn < 0.0f)
                    v = opSub(v, opScale(fn, vn));
            }
            prev[i] = opSub(P, opScale(v, ph.dt));
        }
    }

    void step(const PhysParams& ph, int iters, const std::vector<CapInfo>& caps, int pinned) {
        if (pinned)
            return;
        for (int s = 0; s < sim::kSubsteps; ++s) {
            for (int i = 0; i < n; ++i)
                sub0[i] = pos[i];
            resetContacts();
            verlet(ph);
            for (int it = 0; it < iters; ++it)
                for (int c = 0; c < colorCount; ++c)
                    for (int idx = colorStart[c]; idx < colorStart[c + 1]; ++idx)
                        solveVertex(ph, colorVerts[idx], caps);
            collideAll(ph, caps);
            finalize(ph);
        }
    }
};

struct Metrics {
    float maxV = 0, ke = 0, stretch = 0;
    F3 center{0, 0, 0}, ext{0, 0, 0};
    F3 com{0, 0, 0};
    float maxY = 0;
};

Metrics metrics(const Body& b, std::vector<F3>& prevFramePos, float dt) {
    Metrics m;
    const F3* p = b.pos.data();
    F3 mn = {1e9f, 1e9f, 1e9f}, mx = {-1e9f, -1e9f, -1e9f};
    F3 com = {0, 0, 0};
    float ke2 = 0;
    for (int i = 0; i < b.n; ++i) {
        F3 d = opSub(p[i], prevFramePos[i]);
        float v = len(d) / dt;
        m.maxV = std::max(m.maxV, v);
        ke2 += v * v;
        com = opAdd(com, p[i]);
        mn.x = std::min(mn.x, p[i].x);
        mx.x = std::max(mx.x, p[i].x);
        mn.y = std::min(mn.y, p[i].y);
        mx.y = std::max(mx.y, p[i].y);
        mn.z = std::min(mn.z, p[i].z);
        mx.z = std::max(mx.z, p[i].z);
        m.maxY = std::max(m.maxY, p[i].y);
        prevFramePos[i] = p[i];
    }
    m.ke = 0.5f * ke2 / (float)b.n;
    m.center = opScale(opAdd(mn, mx), 0.5f);
    m.ext = opSub(mx, mn);
    m.com = opScale(com, 1.0f / (float)b.n);
    float st = 0;
    const auto* cons = b.cons;
    for (const auto& c : *cons) {
        float dist = len(opSub(p[c.b], p[c.a]));
        st += std::abs(dist - c.rest) / c.rest;
    }
    m.stretch = (float)std::fabs((double)(st / (float)cons->size()));
    return m;
}

} // namespace

int main(int argc, char** argv) {
    SimParams ui;
    const int FRAMES = (argc > 1) ? (int)std::strtol(argv[1], nullptr, 10) : 240;
    const float FRICTION = (argc > 2) ? std::strtof(argv[2], nullptr) : ui.friction;
    const bool NOCOLS = (argc > 3) && std::strtol(argv[3], nullptr, 10) != 0;
    const bool BEND = (argc > 5) && std::strtol(argv[5], nullptr, 10) != 0;
    const bool SINGLE = (argc > 6) && std::strtol(argv[6], nullptr, 10) != 0;
    const float ZOFF = (argc > 7) ? std::strtof(argv[7], nullptr) : 0.0f;
    const float STIFF = (argc > 8) ? std::strtof(argv[8], nullptr) : ui.stiffness;
    const int CW = 64, CH = 64, holdFrames = 150;
    const float kDefaultSpan = SINGLE ? 3.0f : 8.0f;
    const float kClothSpan = (argc > 4) ? std::strtof(argv[4], nullptr) : kDefaultSpan;
    const float kClothY0 = SINGLE ? 3.5f : 10.0f;
    const int kBallLat = 32, kBallLon = 32;
    const float kBallRadius = 1.5f, kBallY0 = 12.0f;

    RigidScene rigid;
    rigid.init();

    sim::SoftBody cloth, ball;
    {
        std::vector<float> v, nr, co;
        std::vector<sim::Constraint> cons;
        sim::makeCloth(v, nr, co, cons, CW, CH, kClothSpan, kClothY0);
        if (ZOFF != 0.0f)
            for (size_t i = 0; i < v.size(); i += 3)
                v[i + 2] += ZOFF;
        if (BEND) {
            const float sp = kClothSpan / (CW - 1);
            for (int gy = 0; gy < CH; ++gy)
                for (int gx = 0; gx < CW; ++gx) {
                    int i = gy * CW + gx;
                    if (gx < CW - 2)
                        cons.push_back({i, i + 2, 2.0f * sp, 0.5f});
                    if (gy < CH - 2)
                        cons.push_back({i, i + 2 * CW, 2.0f * sp, 0.5f});
                }
        }
        cloth.init(v.data(), nr.data(), co.data(), CW * CH, std::move(cons));
        std::vector<float> bv, bn, bc;
        std::vector<sim::Constraint> bcons;
        std::vector<sim::Triangle> tris;
        sim::makeBall(bv, bn, bc, bcons, tris, kBallLat, kBallLon, kBallRadius, kBallY0);
        ball.init(bv.data(), bn.data(), bc.data(), 2 + (kBallLat - 1) * kBallLon, std::move(bcons));
    }
    Body cb, bb;
    cb.build(cloth);
    bb.build(ball);

    PhysParams ph;
    ph.dt = kFrameDt / sim::kSubsteps;
    ph.damping = ui.damping;
    ph.gravity = sim::kGravity * ui.mass;
    ph.friction = FRICTION;
    ph.skin = 0.01f;
    ph.fricMargin = 0.2f;
    ph.tension = ui.tension;
    ph.stiff = STIFF;
    ph.maxStep = 0.05f;

    std::vector<F3> prevCloth((size_t)CW * CH), prevBall(ball.numVertices());
    for (int i = 0; i < cb.n; ++i)
        prevCloth[i] = cb.pos[i];
    for (int i = 0; i < bb.n; ++i)
        prevBall[i] = bb.pos[i];

    Metrics clothFinal, ballFinal;
    float clothPeakV = 0, clothPeakKe = 0, clothPeakStretch = 0;
    float ballPeakV = 0, ballPeakKe = 0;
    int capCount = 0;
    auto trackPeak = [](float& peakValue, float value) {
        if (!std::isfinite(value))
            peakValue = std::numeric_limits<float>::quiet_NaN();
        else if (value > peakValue)
            peakValue = value;
    };
    for (int f = 0; f < FRAMES; ++f) {
        std::vector<CapsuleGPU> caps;
        if (SINGLE) {
            caps.push_back(CapsuleGPU{{0.f, 1.5f, 0.f, 0.5f}, {0.f, 0.f, 0.f, 1.f}, {0.9f, 0.f, 0.f, 0.f}});
        } else {
            rigid.step(1);
            if (!NOCOLS)
                caps = rigid.capsuleGPU();
        }
        auto ccaps = prepareCaps(caps);
        ph.nCaps = (int)caps.size();
        cb.step(ph, ui.passes, ccaps, f < holdFrames);
        bb.step(ph, ui.passes, ccaps, 0);
        Metrics cm = metrics(cb, prevCloth, kFrameDt);
        Metrics bm = metrics(bb, prevBall, kFrameDt);
        clothFinal = cm;
        ballFinal = bm;
        capCount = (int)ccaps.size();
        trackPeak(clothPeakV, cm.maxV);
        trackPeak(clothPeakKe, cm.ke);
        trackPeak(clothPeakStretch, cm.stretch);
        trackPeak(ballPeakV, bm.maxV);
        trackPeak(ballPeakKe, bm.ke);
    }
    std::printf("fr=%.2f stiff=%.2f span=%.1f single=%d frames=%d caps=%d cloth final maxV=%.3f ke=%.4f stretch=%.4f "
                "maxY=%.2f com=%.2f,%.2f,%.2f peakV=%.3f peakKe=%.4f peakStretch=%.4f ball final maxV=%.3f ke=%.4f "
                "peakV=%.3f peakKe=%.4f\n",
                FRICTION, STIFF, kClothSpan, SINGLE ? 1 : 0, FRAMES, capCount, clothFinal.maxV, clothFinal.ke,
                clothFinal.stretch, clothFinal.maxY, clothFinal.com.x, clothFinal.com.y, clothFinal.com.z, clothPeakV,
                clothPeakKe, clothPeakStretch, ballFinal.maxV, ballFinal.ke, ballPeakV, ballPeakKe);
    return 0;
}
