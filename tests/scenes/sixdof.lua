-- SixDofJoint test: a box welded to the world (all axes fixed) must not fall;
-- a Y-pinned box floats but is free in X/Z and rotation; a control box falls; a box welded to a
-- MOVING kinematic anchor follows it rigidly (folded in from weld_check).

local kFree = 1e30

local function qangle(a, b)
    local d = a[1] * b[1] + a[2] * b[2] + a[3] * b[3] + a[4] * b[4]
    if d > 1 then d = 1 elseif d < -1 then d = -1 end
    return 2 * math.acos(math.abs(d))
end

local function boxBody(name, px, py, pz, mat)
    local g = scene.entity()
    scene.replace(g, "Geometry", mesh.box({ 0.5, 0.5, 0.5 }))
    local shape = scene.entity()
    scene.replace(shape, "BoxShape", { half = { 0.5, 0.5, 0.5 } })
    local col = scene.entity()
    scene.replace(col, "Collider", { shape = shape })
    local e = scene.entity(name)
    scene.replace(e, "Transform", { pos = { px, py, pz } })
    scene.replace(e, "Renderable", { geom = g })
    scene.replace(e, "MaterialRef", { material = mat })
    scene.replace(e, "RigidBody", { colliders = { col } })
    return e
end

local function y(e)
    return scene.get_pose(e).pos[2]
end

local welded, slider, control, lim, motored, live, jlive
local hand2, obj2, Qh, Qo, r30, y30
local n = 0

function _on_ready(self)
    local ground = scene.entity("ground")
    scene.replace(ground, "Transform", { pos = { 0, -1, 0 } })
    scene.replace(ground, "Renderable", { geom = (function()
        local g = scene.entity()
        scene.replace(g, "Geometry", mesh.box({ 55, 1, 55 }))
        return g
    end)() })
    scene.replace(ground, "MaterialRef", { material = scene.get_entity("mat_ground") })
    local gShape = scene.entity()
    scene.replace(gShape, "BoxShape", { half = { 40, 1, 40 } })
    local gCol = scene.entity()
    scene.replace(gCol, "Collider", { shape = gShape })
    scene.replace(ground, "KinematicBody", { colliders = { gCol } })

    welded = boxBody("weld", 0, 1.0, 0, scene.get_entity("mat_red"))
    local jw = scene.entity()
    scene.replace(jw, "SixDofJoint", {
        bodyA = welded, grabPos = { 0, 1.0, 0 },
        limOn = 1,
        limTransMin = { 0, 0, 0 }, limTransMax = { 0, 0, 0 },
        limAngMin = { 0, 0, 0 }, limAngMax = { 0, 0, 0 } })

    slider = boxBody("slide", 2, 1.0, 0, scene.get_entity("mat_blue"))
    local js = scene.entity()
    scene.replace(js, "SixDofJoint", {
        bodyA = slider, grabPos = { 2, 1.0, 0 },
        limOn = 1,
        limTransMin = { -kFree, 0, -kFree }, limTransMax = { kFree, 0, kFree },
        limAngMin = { -kFree, -kFree, -kFree }, limAngMax = { kFree, kFree, kFree } })

    control = boxBody("free", -2, 1.0, 0, scene.get_entity("mat_ground"))

    -- finite Y limit only (no motor): must hold the box at y = 1.0
    lim = boxBody("lim", 6, 1.0, 0, scene.get_entity("mat_blue"))
    local jl = scene.entity()
    scene.replace(jl, "SixDofJoint", {
        bodyA = lim, grabPos = { 6, 1.0, 0 },
        limOn = 1,
        limTransMin = { -1, 0, -1 }, limTransMax = { 1, 1.1, 1 },
        limAngMin = { -kFree, -kFree, -kFree }, limAngMax = { kFree, kFree, kFree } })

    -- position motor on TranslationY drives the box to y = 1.0 + 0.5
    motored = boxBody("motor", 4, 1.0, 0, scene.get_entity("mat_orange"))
    local jm = scene.entity()
    scene.replace(jm, "SixDofJoint", {
        bodyA = motored, grabPos = { 4, 1.0, 0 },
        limOn = 1,
        limTransMin = { -1, 0, -1 }, limTransMax = { 1, 1.1, 1 },
        limAngMin = { -kFree, -kFree, -kFree }, limAngMax = { kFree, kFree, kFree },
        motorState = { 0, 2, 0, 0, 0, 0 },
        motorFreq = { 0, 8, 0 }, motorDamp = { 0, 6, 0 },
        motorTargetPos = { 0, 0.5, 0 } })

    -- live-sync: starts free (falls), fixed at frame 40 (pulled back to 1.0), freed at 80 (falls again)
    live = boxBody("live", 8, 1.0, 0, scene.get_entity("mat_red"))
    jlive = scene.entity()
    scene.replace(jlive, "SixDofJoint", {
        bodyA = live, grabPos = { 8, 1.0, 0 },
        limOn = 1,
        limTransMin = { -kFree, -kFree, -kFree }, limTransMax = { kFree, kFree, kFree },
        limAngMin = { -kFree, -kFree, -kFree }, limAngMax = { kFree, kFree, kFree } })

    -- moving-anchor weld (from weld_check): a kinematic "hand" carries a welded box; the box must
    -- follow rigidly (position + relative orientation) when the hand translates and rolls
    Qh = math.qaxis({ 0, 1, 0 }, 0.5)
    hand2 = scene.entity("weld_hand")
    scene.replace(hand2, "Transform", { pos = { 10, 5, 0 }, quat = Qh })
    local hs = scene.entity()
    scene.replace(hs, "CapsuleShape", { halfLen = 0.06, radius = 0.03 })
    local hc = scene.entity()
    scene.replace(hc, "Collider", { shape = hs })
    scene.replace(hand2, "KinematicBody", { colliders = { hc } })
    Qo = math.qaxis({ 1, 0, 0 }, 0.9)
    local s2 = scene.entity()
    scene.replace(s2, "BoxShape", { half = { 0.5, 0.5, 0.5 } })
    local c2 = scene.entity()
    scene.replace(c2, "Collider", { shape = s2 })
    obj2 = scene.entity("weld_obj")
    scene.replace(obj2, "Transform", { pos = { 10, 5, 2 }, quat = Qo })
    scene.replace(obj2, "RigidBody", { colliders = { c2 } })
end

local tick = 0
function _physics(self, dt)
    tick = tick + 1
    if tick == 5 then
        local relQ = math.qmul(math.qinv(Qo), Qh) -- qinv(bodyB)*bodyA: rigid carry, no snap
        local j = scene.entity()
        scene.replace(j, "SixDofJoint", {
            bodyA = hand2, bodyB = obj2, grabPos = { 10, 5, 2 }, relQuat = relQ, limOn = 1,
            limTransMin = { 0, 0, 0 }, limTransMax = { 0, 0, 0 },
            limAngMin = { 0, 0, 0 }, limAngMax = { 0, 0, 0 } })
    elseif tick == 30 then
        local hq = scene.world_pose(hand2).quat
        r30 = math.qmul(math.qinv(hq), scene.world_pose(obj2).quat)
        y30 = scene.world_pose(obj2).pos[2]
    elseif tick == 31 then
        scene.replace(hand2, "Transform", { pos = { 10, 7, 0 }, quat = math.qmul(math.qaxis({ 0, 0, 1 }, 0.5), Qh) })
    elseif tick == 60 then
        local hq = scene.world_pose(hand2).quat
        local r60 = math.qmul(math.qinv(hq), scene.world_pose(obj2).quat)
        t.check("weld: obj held at ~5", math.abs(y30 - 5.0) < 0.2)
        t.check("weld: obj follows hand to ~7", math.abs(scene.world_pose(obj2).pos[2] - 7.0) < 0.2)
        t.check("weld: rel drift ~0", qangle(r60, r30) < 0.05)
    end
end

function _animate(self, dt)
    n = n + 1
    if n % 30 == 0 then
        print(string.format("sixdof: weld=%.3f slide=%.3f free=%.3f lim=%.3f (want 1.000) mot=%.3f (want 1.500) live=%.3f",
                          y(welded), y(slider), y(control), y(lim), y(motored), y(live)))
    end
    if n == 40 then
        scene.replace(jlive, "SixDofJoint", {
            limTransMin = { 0, 0, 0 }, limTransMax = { 0, 0, 0 } })
    end
    if n == 70 then
        t.eq("live fixed live: pulled back to 1.0", y(live), 1.0, 0.05)
    end
    if n == 80 then
        scene.replace(jlive, "SixDofJoint", {
            limTransMin = { -kFree, -kFree, -kFree }, limTransMax = { kFree, kFree, kFree } })
    end
    if n == 150 then
        t.eq("weld held at 1.0", y(welded), 1.0, 0.05)
        t.eq("slide held at 1.0", y(slider), 1.0, 0.05)
        t.check("free fell to ground", y(control) < 0.7)
        t.eq("lim held at 1.0", y(lim), 1.0, 0.05)
        t.eq("motor at target 1.5", y(motored), 1.5, 0.1)
        t.check("live freed live: fell again", y(live) < 0.7)
        t.done()
    end
end

