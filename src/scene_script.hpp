#pragma once
#include "math.hpp"
#include <map>
#include <string>
#include <vector>

struct lua_State;
struct World;

// Embedded Lua 5.4 host for scene behavior scripts (scene .json + .lua).
// Scripts see only the `scene` table (io/os/debug/package are removed); errors go to
// stderr and never crash the engine.
class SceneScript {
public:
    SceneScript() = default;
    ~SceneScript() { shutdown(); }
    SceneScript(const SceneScript&) = delete;
    SceneScript& operator=(const SceneScript&) = delete;

    void init(World& w);
    // load + run the script file (the JSON data must already be built); false on error
    bool run(const std::string& path, std::string& err);
    // run all frame callbacks; no-op without a script
    void frame(float dt);
    bool hasButton(const std::string& name) const;
    void button(const std::string& name);
    void shutdown();

private:
    World& w() const { return *m_w; }
    int newRef(lua_State* L, int idx);
    void dropRef(int ref);
    // invoke an anim callback fn(dt) -> (pos, quat); reports an error once; false on failure
    bool callPoseRef(int ref, float dt, V3& pos, V4& quat);
    // the error object is on top of the stack
    void reportError(const char* what);

    static SceneScript* sSelf(lua_State* L);
    static int lMaterial(lua_State* L);
    static int lCapsule(lua_State* L);
    static int lCloth(lua_State* L);
    static int lBall(lua_State* L);
    static int lSet(lua_State* L);
    static int lGet(lua_State* L);
    static int lTime(lua_State* L);
    static int lEntity(lua_State* L);
    static int lFrame(lua_State* L);
    static int lAnim(lua_State* L);
    static int lButton(lua_State* L);
    static int lResetSofts(lua_State* L);
    static int lvAdd(lua_State* L);
    static int lvSub(lua_State* L);
    static int lvScale(lua_State* L);
    static int lvLen(lua_State* L);
    static int lvNorm(lua_State* L);
    static int lvCross(lua_State* L);
    static int lvDot(lua_State* L);
    static int lqMul(lua_State* L);
    static int lqInv(lua_State* L);
    static int lqAxis(lua_State* L);
    static int lqRot(lua_State* L);
    static int lqFromTo(lua_State* L);

    World* m_w = nullptr;
    lua_State* L = nullptr;
    std::vector<int> m_frames;            // registry refs of frame callbacks
    std::map<std::string, int> m_buttons; // action name -> registry ref
    std::vector<int> m_refs;              // every registry ref (unrefed at shutdown)
    std::vector<int> m_errored;           // anim refs that already reported an error
    float m_time = 0.0f;
};
