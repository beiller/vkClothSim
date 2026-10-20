#pragma once
#include "math.hpp"

struct View {
    V3 pos{0, 0, 0};
    V4 quat{0, 0, 0, 1};
    float tanL = 1.0f, tanR = 1.0f, tanU = 0.58f, tanD = 0.58f;
};

struct XrController {
    bool active = false;
    V3 pos{};
    V4 quat{0, 0, 0, 1};
    V3 aimPos{};
    V4 aimQuat{0, 0, 0, 1};
    float triggers[2] = {}; // 0 = trigger, 1 = squeeze (0..1)
    float axis[2] = {};
    bool buttons[2] = {};   // 0 = A, 1 = B (pressed)
};

struct XrFrame {
    bool running = false;
    bool shouldRender = false;
    bool havePose = false;
    int nEyes = 0;
    View eye[2]{};
    XrController controller[2]{}; // 0 = left hand, 1 = right hand
};
