#pragma once
#include "reflect.hpp"
#include <entt/entt.hpp>
#include <string>

struct Engine;

struct UrlRef {
    const FieldDesc* field = nullptr;
    void* base = nullptr;
};

bool resolveUrl(const Engine& w, const std::string& url, UrlRef& out, std::string& err);
bool resolveEntityField(const Engine& w, entt::entity ent, const std::string& field, const std::string& label,
                        UrlRef& out, std::string& err);
bool sceneSet(Engine& w, const std::string& url, const float* vals, int n, std::string& err);
bool sceneGet(const Engine& w, const std::string& url, float* vals, int maxN, int& n, std::string& err);
bool sceneSetEnt(Engine& w, entt::entity ent, const std::string& field, const float* vals, int n, std::string& err);
bool sceneGetEnt(const Engine& w, entt::entity ent, const std::string& field, float* vals, int maxN, int& n,
                 std::string& err);
