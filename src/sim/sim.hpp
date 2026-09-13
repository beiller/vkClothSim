#pragma once
#include "math.hpp"
#include "mesh.hpp"
#include <cmath>
#include <cstdint>
#include <vector>

namespace sim {

inline constexpr float kGravity = -9.81f;
inline constexpr int kSubsteps = 3;

struct Constraint {
    int a, b;
    float rest;
    float k;
};

struct Entry {
    int j;
    float rest;
    float k;
    int pad;
};

struct SoftBody {
    int n = 0;
    std::vector<float> vtx;
    std::vector<float> pos4;
    std::vector<Constraint> cons;
    std::vector<Entry> entries;
    std::vector<int> entryStart;
    int colorCount = 0;
    std::vector<int> colorStart;
    std::vector<int> colorVerts;

    void init(const Mesh& mesh, const std::vector<Constraint>& c) {
        n = mesh.vertexCount();
        vtx = mesh.vtx;
        cons = c;
        pos4.resize((size_t)4 * n);
        for (int i = 0; i < n; ++i) {
            const size_t p = (size_t)4 * i, o = (size_t)12 * i;
            pos4[p] = vtx[o];
            pos4[p + 1] = vtx[o + 1];
            pos4[p + 2] = vtx[o + 2];
            pos4[p + 3] = 0.0f;
        }
        buildEntries();
        buildColoring();
    }

private:
    void buildEntries() {
        entryStart.assign(n + 1, 0);
        for (const auto& c : cons) {
            ++entryStart[c.a + 1];
            ++entryStart[c.b + 1];
        }
        for (int i = 1; i <= n; ++i)
            entryStart[i] += entryStart[i - 1];
        entries.resize((size_t)entryStart[n]);
        std::vector<int> cur(n);
        for (int i = 0; i < n; ++i)
            cur[i] = entryStart[i];
        for (const auto& c : cons) {
            entries[cur[c.a]++] = {c.b, c.rest, c.k, 0};
            entries[cur[c.b]++] = {c.a, c.rest, c.k, 0};
        }
    }

    void buildColoring() {
        std::vector<std::vector<int>> adj(n);
        for (int i = 0; i < n; ++i)
            for (int e = entryStart[i]; e < entryStart[i + 1]; ++e)
                adj[i].push_back(entries[e].j);
        std::vector<int> vertexColor(n, -1);
        std::vector<std::vector<int>> groups;
        for (int i = 0; i < n; ++i) {
            bool used[64] = {};
            for (int nb : adj[i])
                if (vertexColor[nb] >= 0 && vertexColor[nb] < 64)
                    used[vertexColor[nb]] = true;
            size_t c = 0;
            while (c < 64 && used[c])
                ++c;
            if (c == groups.size())
                groups.resize(c + 1);
            vertexColor[i] = (int)c;
            groups[c].push_back(i);
        }
        colorCount = (int)groups.size();
        colorStart.assign(colorCount + 1, 0);
        for (int c = 0; c < colorCount; ++c)
            colorStart[c + 1] = colorStart[c] + (int)groups[c].size();
        colorVerts.resize(n);
        for (int c = 0; c < colorCount; ++c)
            for (size_t j = 0; j < groups[c].size(); ++j)
                colorVerts[colorStart[c] + (int)j] = groups[c][j];
    }
};

} // namespace sim
