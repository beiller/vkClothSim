-- 20-link capsule rope: 25cm links (10cm diameter) chained with SixDofJoints,
-- translation fixed (0, 0, 0), rotation limited to +/-20 deg. All links are
-- dynamic; the rope starts horizontal at y = 2 and falls to the ground.
local M = {}

local kN = 20
local kLen = 0.25
local kRad = 0.05
local kHalfLen = (kLen - 2 * kRad) / 2 -- 0.075: capsule end-to-end length = kLen
local kLim = math.rad(20)
local kR = math.sqrt(0.5) -- exactly-normal 90 deg quat (Jolt asserts normalized)
local kY = 2.0 -- rope height
local kX0 = -(kN - 1) * kLen / 2 -- left link center; rope spans x = -2.5 .. 2.5

-- kinematic "handle" loop that drags link 1 so the chain never settles/sleeps
local kCx, kCy, kHandleR = -1.0, 2.0, 1.5 -- loop center + radius (y stays above the floor)
local kW = 1.5                               -- rad/s
local kPhase = math.pi                       -- start at the leftmost point (link 1 center)

local function link(i, mat)
    local x = kX0 + (i - 1) * kLen
    local g = scene.entity()
    scene.replace(g, "Geometry", mesh.capsule({ radius = kRad, halfLen = kHalfLen, S = 16, M = 16 }))
    local shape = scene.entity()
    scene.replace(shape, "CapsuleShape", { halfLen = kHalfLen, radius = kRad })
    local col = scene.entity()
    scene.replace(col, "Collider", { shape = shape })
    local e = scene.entity("link" .. i)
    scene.replace(e, "Transform", { pos = { x, kY, 0 }, quat = { 0, 0, kR, kR } }) -- 90 deg about Z: capsule long axis along X
    scene.replace(e, "Renderable", { geom = g })
    scene.replace(e, "MaterialRef", { material = mat })
    -- 25 g per link (~0.5 kg for the whole rope): a 10mm cord is ~100 g/m
    scene.replace(e, "RigidBody", { colliders = { col }, mass = 0.025 })
    return e
end

local links = {}

function M._on_ready(self)
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

    local mat = { scene.get_entity("mat_red"), scene.get_entity("mat_blue"), scene.get_entity("mat_orange") }
    for i = 1, kN do
        links[i] = link(i, mat[i % 3 + 1])
    end

    local function joint(bodyA, bodyB, px, py, pz)
        local j = scene.entity()
        local spec = {
            bodyA = bodyA,
            bodyB = bodyB,
            grabPos = { px, py, pz },
            limOn = 1,
            limTransMin = { 0, 0, 0 }, limTransMax = { 0, 0, 0 },
            limAngMin = { -kLim, -kLim, -kLim }, limAngMax = { kLim, kLim, kLim } }
        scene.replace(j, "SixDofJoint", spec)
    end

    -- chain joints: shared end point between consecutive links
    for i = 1, kN - 1 do
        joint(links[i], links[i + 1], kX0 + (i - 1) * kLen + kLen / 2, kY, 0)
    end

    -- kinematic handle on link 1, animated in _physics: keeps the chain awake
    local hs = scene.entity()
    scene.replace(hs, "BoxShape", { half = { 0.05, 0.05, 0.05 } })
    local hc = scene.entity()
    scene.replace(hc, "Collider", { shape = hs })
    local h = scene.entity("handle")
    scene.replace(h, "Transform", { pos = { kCx - kHandleR, kCy, 0 }, quat = { 0, 0, 0, 1 } })
    scene.replace(h, "KinematicBody", { colliders = { hc } })
    self.handle, self.t = h, 0
    local hj = scene.entity()
    scene.replace(hj, "Joint", { bodyA = h, bodyB = links[1], type = 0,
                                grabPos = { kCx - kHandleR, kCy, 0 } })
end

function M._physics(self, dt)
    self.t = self.t + dt
    local a = kPhase + kW * self.t
    scene.set_pose(self.handle, { kCx + kHandleR * math.cos(a), kCy + kHandleR * math.sin(a), 0 },
        { 0, 0, 0, 1 })
    if self.t > 1.0 and not self.checked then -- ~1s in: the handle loop must have dragged link 1 off its start x
        self.checked = true
        local x1 = scene.world_pose(links[1]).pos[1]
        print(string.format("ROPE t=%.2f link1 x=%.3f (start %.3f)", self.t, x1, kX0))
        t.check("link1 dragged off start x", math.abs(x1 - kX0) > 0.1, string.format("x=%.3f", x1))
        t.done()
    end
end

return M
