#pragma once
// scene URLs: "name.field" addressable state. name = entity Name (or reserved root
// "render"/"rigid"); field = reflect.hpp table entry ("a.b" nested names allowed;
// "name.Component.field" disambiguates).
#include "reflect.hpp"
#include <string>

struct World;

struct UrlRef {
    const FieldDesc* field = nullptr;
    void* base = nullptr;
};

bool resolveUrl(World& w, const std::string& url, UrlRef& out, std::string& err);
// n = 1 for scalars, 3 for Color/DragV3
bool sceneSet(World& w, const std::string& url, const float* vals, int n, std::string& err);
