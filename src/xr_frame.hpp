#pragma once
#include "math.hpp"

// one controller, in the reference space; no OpenXR types
struct XrController {
    bool active = false; // has a valid pose this frame
    V3 pos{};            // grip pose position (ref space)
    V4 quat{0, 0, 0, 1}; // grip pose orientation
    V3 aimPos{};         // aim pose position (front of the controller)
    V4 aimQuat{0, 0, 0, 1};
    float trigger = 0.0f; // 0..1 pull
};

// per-frame XR state: head + per-eye poses in the reference space; no OpenXR types
struct XrFrame {
    bool running = false;
    bool shouldRender = false;
    bool havePose = false;
    int nEyes = 0;
    XrEyeData eye[2]{};
    XrController controller[2]{}; // 0 = left hand, 1 = right hand
    int nControllers = 0;
};
