-- tapered-capsule test: dynamic tapered capsules fall onto a static ground.
-- the visual mesh (mesh.taperedCapsule) and the collision shape (TaperedCapsule) share one
-- param set, so a resting body should sit snug on its own silhouette.
local M = {}

local function geom(t)
    local g = scene.entity()
    scene.replace(g, "Geometry", t)
    return g
end

local kTapers = {
    { halfHeight = 0.5, bottomRadius = 0.25, topRadius = 0.15 }, -- taper to top
    { halfHeight = 0.4, bottomRadius = 0.3, topRadius = 0.3 }, -- uniform (plain capsule)
    { halfHeight = 0.7, bottomRadius = 0.15, topRadius = 0.45 }, -- taper to bottom
    { halfHeight = 0.3, bottomRadius = 0.4, topRadius = 0.1 }, -- strong taper
}

function M._on_ready(self)
    local ground = scene.entity("ground")
    scene.replace(ground, "Transform", { pos = { 0, -1, 0 } })
    scene.replace(ground, "Renderable", { geom = geom(mesh.box({ 55, 1, 55 })) })
    scene.replace(ground, "MaterialRef", { material = scene.find("mat_ground") })
    local gShape = scene.entity()
    scene.replace(gShape, "BoxShape", { half = { 40, 1, 40 } })
    local gCol = scene.entity()
    scene.replace(gCol, "Collider", { shape = gShape })
    scene.replace(ground, "RigidBody", { isStatic = 1, colliders = { gCol } })
    soft.floor(0.0001)

    math.randomseed(12345)
    for i, t in ipairs(kTapers) do
        local shape = scene.entity()
        scene.replace(shape, "TaperedCapsule", t)
        local col = scene.entity()
        scene.replace(col, "Collider", { shape = shape })
        local capGeom = geom(mesh.taperedCapsule(t))
        for _ = 1, 2 do
            local px = (math.random() * 2 - 1) * 3.0
            local py = 4.0 + 2.5 * math.random()
            local pz = (math.random() * 2 - 1) * 3.0
            local q = scene.qmul(scene.qaxis({ 0, 1, 0 }, math.random() * 2 * math.pi),
                                scene.qaxis({ 1, 0, 0 }, (math.random() * 2 - 1) * 1.2))
            local e = scene.entity()
            scene.replace(e, "Transform", { pos = { px, py, pz }, quat = q })
            scene.replace(e, "Renderable", { geom = capGeom })
            scene.replace(e, "MaterialRef", { material = scene.find("mat_capsule") })
            scene.replace(e, "RigidBody", { colliders = { col } })
        end
    end
end

return M
