#pragma once
#include "math.hpp"

// one render eye of the headset (or synthesized virtual head); no OpenXR types
struct XrEyeData {
    V3 pos{0, 0, 0};
    V4 quat{0, 0, 0, 1};
    float tanL = 1.0f, tanR = 1.0f, tanU = 0.58f, tanD = 0.58f;
};

// one controller, in the reference space; no OpenXR types
struct XrController {
    bool active = false; // has a valid pose this frame
    V3 pos{};            // grip pose position (ref space)
    V4 quat{0, 0, 0, 1}; // grip pose orientation
    V3 aimPos{};         // aim pose position (front of the controller)
    V4 aimQuat{0, 0, 0, 1};
    float triggers[2] = {}; // 0 = trigger, 1 = squeeze (0..1)
    float axis[2] = {};     // stick x=right, y=forward (-1..1)
    bool buttons[2] = {};   // 0 = A, 1 = B (pressed)
};

// per-frame XR state: head + per-eye poses in the reference space; no OpenXR types
struct XrFrame {
    bool running = false;
    bool shouldRender = false;
    bool havePose = false;
    int nEyes = 0;
    XrEyeData eye[2]{};
    XrController controller[2]{}; // 0 = left hand, 1 = right hand
};
