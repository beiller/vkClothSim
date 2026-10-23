#pragma once
#include "math.hpp"

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
    bool focused = false;
    bool shouldRender = false;
    bool havePose = false;
    int nEyes = 0;
    View eye[2]{};
    XrController controller[2]{}; // 0 = left hand, 1 = right hand
};
