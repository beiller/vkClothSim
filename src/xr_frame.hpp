#pragma once
#include "math.hpp"

// per-frame XR state: head + per-eye poses in the reference space; no OpenXR types
struct XrFrame {
    bool running = false;
    bool shouldRender = false;
    bool havePose = false;
    int nEyes = 0;
    XrEyeData eye[2]{};
};
