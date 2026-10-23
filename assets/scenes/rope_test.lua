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
    scene.replace(ground, "MaterialRef", { material = scene.find("mat_ground") })
    local gShape = scene.entity()
    scene.replace(gShape, "BoxShape", { half = { 40, 1, 40 } })
    local gCol = scene.entity()
    scene.replace(gCol, "Collider", { shape = gShape })
    scene.replace(ground, "RigidBody", { isStatic = 1, colliders = { gCol } })

    local mat = { scene.find("mat_red"), scene.find("mat_blue"), scene.find("mat_orange") }
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
end

return M
