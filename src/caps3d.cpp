// 3dsim – raylib 3D capsule viewer.
// Renders the rigid "body parts as capsules" skeleton (the collision proxies the
// soft-skin compute will later be driven by). Orbit camera: drag to rotate, wheel
// to zoom, ESC to quit. `--shot out.png` renders one frame to a file and exits.
#include <raylib.h>
#include <raymath.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

struct Capsule {
    Vector3 a, b;
    float r;
    Color c;
};

struct Drawn {
    Capsule cap;
    Material mat;
};

// Rotation that maps the local +Y axis onto `dir` (capsules are built along +Y).
static Matrix alignYto(const Vector3& dir) {
    const Vector3 up{0, 1, 0};
    Vector3 cr = Vector3CrossProduct(up, dir);
    float cl = Vector3Length(cr);
    if (cl < 1e-6f) {
        if (Vector3DotProduct(up, dir) < 0.0f)
            return MatrixRotate((Vector3){0, 1, 0}, 3.14159265f); // dir ~ -up
        return MatrixIdentity();
    }
    float ang = atan2f(cl, Vector3DotProduct(up, dir));
    Quaternion q = QuaternionFromAxisAngle(Vector3Normalize(cr), ang);
    return QuaternionToMatrix(q);
}

// A simple articulated humanoid, feet at y=0, ~1.76 tall. One capsule per part.
static std::vector<Capsule> humanoid() {
    auto C = [](float x, float y, float z) { return (Vector3){x, y, z}; };
    std::vector<Capsule> v;
    auto add = [&](Vector3 a, Vector3 b, float r, Color c) { v.push_back((Capsule){a, b, r, c}); };

    // torso
    add(C(0, 0.92, 0), C(0, 1.06, 0), 0.15, PINK);     // pelvis
    add(C(0, 1.06, 0), C(0, 1.36, 0), 0.14, RED);      // spine
    add(C(0, 1.36, 0), C(0, 1.52, 0), 0.17, ORANGE);   // chest
    add(C(0, 1.60, 0), C(0, 1.72, 0), 0.105, SKYBLUE); // head

    for (int s : {-1, 1}) {
        float x = 0.12f * s;
        // arms
        add(C(0.16f * s, 1.50f, 0), C(0.29f * s, 1.48f, 0), 0.075f, LIGHTGRAY); // shoulder
        add(C(0.29f * s, 1.48f, 0), C(0.33f * s, 1.16f, 0.02f * s), 0.06f, ORANGE); // upper arm
        add(C(0.33f * s, 1.16f, 0.02f * s), C(0.35f * s, 0.86f, -0.02f * s), 0.05f, ORANGE); // forearm
        add(C(0.35f * s, 0.86f, -0.02f * s), C(0.37f * s, 0.68f, -0.04f * s), 0.045f, YELLOW); // hand
        // legs
        float lx = x;
        add(C(lx, 0.92f, 0), C(lx, 0.50f, 0), 0.085f, GREEN); // thigh
        add(C(lx, 0.50f, 0), C(lx, 0.07f, 0), 0.065f, GREEN); // shin
        add(C(lx, 0.07f, 0), C(lx, 0.07f, 0.22f), 0.055f, BROWN); // foot
    }
    return v;
}

static void drawAll(const std::vector<Drawn>& parts, Mesh cyl, Mesh sph) {
    for (const auto& d : parts) {
        const Capsule& k = d.cap;
        float L = Vector3Length(Vector3Subtract(k.b, k.a));
        Vector3 mid = Vector3Scale(Vector3Add(k.a, k.b), 0.5f);
        Vector3 dir = Vector3Normalize(Vector3Subtract(k.b, k.a));
        Matrix rot = alignYto(dir);

        // cylinder = straight segment A->B (scaled unit cyl), spheres cap each end
        Matrix cylT = MatrixMultiply(MatrixTranslate(mid.x, mid.y, mid.z), MatrixMultiply(rot, MatrixScale(k.r, L, k.r)));
        DrawMesh(cyl, d.mat, cylT);
        DrawMesh(sph, d.mat, MatrixMultiply(MatrixTranslate(k.a.x, k.a.y, k.a.z), MatrixScale(k.r, k.r, k.r)));
        DrawMesh(sph, d.mat, MatrixMultiply(MatrixTranslate(k.b.x, k.b.y, k.b.z), MatrixScale(k.r, k.r, k.r)));
        DrawLine3D(k.a, k.b, WHITE); // rigid axis
    }
}

int main(int argc, char** argv) {
    bool shot = false;
    std::string shotPath = "caps.png";
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--shot") {
            shot = true;
            if (i + 1 < argc)
                shotPath = argv[++i];
        }
    }

    InitWindow(900, 900, "3dsim");
    SetTargetFPS(60);

    Mesh cyl = GenMeshCylinder(1.0f, 1.0f, 24);
    UploadMesh(&cyl, false);
    Mesh sph = GenMeshSphere(1.0f, 16, 20);
    UploadMesh(&sph, false);

    std::vector<Drawn> parts;
    for (auto& k : humanoid()) {
        Material m = LoadMaterialDefault();
        m.maps[MATERIAL_MAP_DIFFUSE].color = k.c;
        parts.push_back((Drawn){k, m});
    }

    Camera3D camera{(Vector3){2.6f, 1.5f, 3.2f}, (Vector3){0, 0.9f, 0}, (Vector3){0, 1, 0}, 45.0f, CAMERA_PERSPECTIVE};

    auto renderFrame = [&](Camera3D cam) {
        ClearBackground((Color){28, 28, 34, 255});
        BeginMode3D(cam);
        DrawGrid(10, 1.0f);
        drawAll(parts, cyl, sph);
        EndMode3D();
    };

    if (shot) {
        UpdateCamera(&camera, CAMERA_ORBITAL);
        RenderTexture2D rt = LoadRenderTexture(900, 900);
        BeginTextureMode(rt);
        renderFrame(camera);
        EndTextureMode();
        Image img = LoadImageFromTexture(rt.texture);
        bool ok = ExportImage(img, shotPath.c_str());
        std::printf("wrote %s (%s)\n", shotPath.c_str(), ok ? "ok" : "FAIL");
        UnloadImage(img);
        UnloadRenderTexture(rt);
    } else {
        std::printf("3dsim: %zu capsules | drag=orbit wheel=zoom esc=quit\n", parts.size());
        while (!WindowShouldClose()) {
            UpdateCamera(&camera, CAMERA_ORBITAL);
            BeginDrawing();
            renderFrame(camera);
            DrawText("3dsim | drag=orbit  wheel=zoom  esc=quit", 12, 10, 20, WHITE);
            EndDrawing();
        }
    }

    UnloadMesh(cyl);
    UnloadMesh(sph);
    for (auto& d : parts)
        UnloadMaterial(d.mat);
    CloseWindow();
    return 0;
}
