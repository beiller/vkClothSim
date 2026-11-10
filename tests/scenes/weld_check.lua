local tick = 0

local function qangle(a, b)
    local d = a[1] * b[1] + a[2] * b[2] + a[3] * b[3] + a[4] * b[4]
    if d > 1 then d = 1 elseif d < -1 then d = -1 end
    return 2 * math.acos(math.abs(d))
end

local Qh, Qo
local obj, hand
local r30, y30

function _on_ready(self)
    Qh = math.qaxis({ 0, 1, 0 }, 0.5)
    hand = scene.entity("hand")
    scene.replace(hand, "Transform", { pos = { 0, 5, 0 }, quat = Qh })
    local hs = scene.entity()
    scene.replace(hs, "CapsuleShape", { halfLen = 0.06, radius = 0.03 })
    local hc = scene.entity()
    scene.replace(hc, "Collider", { shape = hs })
    scene.replace(hand, "KinematicBody", { colliders = { hc } })

    Qo = math.qaxis({ 1, 0, 0 }, 0.9)
    local s = scene.entity()
    scene.replace(s, "BoxShape", { half = { 0.5, 0.5, 0.5 } })
    local c = scene.entity()
    scene.replace(c, "Collider", { shape = s })
    obj = scene.entity("obj")
    scene.replace(obj, "Transform", { pos = { 0, 5, 2 }, quat = Qo })
    scene.replace(obj, "RigidBody", { colliders = { c } })
end

function _physics(self, dt)
    tick = tick + 1
    if tick == 5 then
        local relQ = math.qmul(math.qinv(Qo), Qh) -- qinv(bodyB)*bodyA
        local j = scene.entity()
        scene.replace(j, "SixDofJoint", {
            bodyA = hand, bodyB = obj, grabPos = { 0, 5, 2 }, relQuat = relQ, limOn = 1,
            limTransMin = { 0, 0, 0 }, limTransMax = { 0, 0, 0 },
            limAngMin = { 0, 0, 0 }, limAngMax = { 0, 0, 0 } })
        print("WELD created")
    elseif tick == 30 then
        local hq = scene.world_pose(hand).quat
        r30 = math.qmul(math.qinv(hq), scene.world_pose(obj).quat)
        y30 = scene.world_pose(obj).pos[2]
        print(string.format("WELD t=30 objY=%.3f (held ~5)", y30))
    elseif tick == 31 then
        local hq = math.qmul(math.qaxis({ 0, 0, 1 }, 0.5), Qh) -- +2 up, +0.5 rad roll
        scene.replace(hand, "Transform", { pos = { 0, 7, 0 }, quat = hq })
    elseif tick == 60 then
        local hq = scene.world_pose(hand).quat
        local oq = scene.world_pose(obj).quat
        local r60 = math.qmul(math.qinv(hq), oq)
        print(string.format("WELD t=60 objY=%.3f (follows ~7) rel_drift=%.3f (expect ~0)",
            scene.world_pose(obj).pos[2], qangle(r60, r30)))
        t.check("obj held at ~5", math.abs(y30 - 5.0) < 0.2)
        t.check("obj follows hand to ~7", math.abs(scene.world_pose(obj).pos[2] - 7.0) < 0.2)
        t.check("weld rel drift ~0", qangle(r60, r30) < 0.05)
        t.done()
    end
end

