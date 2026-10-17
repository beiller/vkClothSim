-- Bone axis probe: a thin green arrow along each bone's local +Y (shaft + tip), parented to the
-- joint entity so it moves and turns with the bone.
-- head + neckUpper yaw continuously (different frequencies) so the axis motion is watchable.
local M = {}
local bind = {}

local function bindPose(name)
    if not bind[name] then
        local p = scene.get_pose(name)
        bind[name] = { p[1], p[2] }
    end
    return bind[name]
end

local function yaw(name, ang)
    local b = bindPose(name)
    if ang == 0 then
        scene.set_pose(name, b[1], b[2])
    else
        scene.set_pose(name, b[1], scene.qmul(b[2], scene.qaxis({ 0, 1, 0 }, ang)))
    end
end

-- stdlib geometry tables are the Geometry struct itself: stash one in a geometry entity
local function geom(t)
    local g = scene.entity()
    scene.replace(g, "Geometry", t)
    return g
end

function M._on_ready(self)
    local matGround = scene.entity("mat_ground")
    scene.replace(matGround, "Material", { baseColor = {0.35, 0.35, 0.38}, metallic = 0, roughness = 0.95 })
    local ground = scene.entity("ground")
    scene.replace(ground, "Transform", { pos = { 0, -1, 0 } })
    scene.replace(ground, "Renderable", { geom = geom(mesh.box({ 20, 1, 20 })) })
    scene.replace(ground, "MaterialRef", { material = matGround })

    local ch = scene.entity("char")
    scene.replace(ch, "Transform", { pos = { 0, 0, 0 } })
    render.gltf(ch, "local/basegen8.glb")

    local matAxis = scene.entity("mat_axis")
    scene.replace(matAxis, "Material", { baseColor = {0, 1, 0.4}, metallic = 0, roughness = 0.4,
                                         emissive = {0, 1, 0.4} })
    local shaft = geom(mesh.box({ 0.004, 0.07, 0.004 }))
    local tip = geom(mesh.sphere({ subdiv = 1, radius = 0.008 }))
    for _, j in ipairs(scene.joints(ch)) do
        local s = scene.entity()
        scene.replace(s, "Transform", { pos = { 0, 0.035, 0 } })
        scene.replace(s, "Renderable", { geom = shaft, noShadow = true })
        scene.replace(s, "MaterialRef", { material = matAxis })
        scene.add_child(j, s)
        local t = scene.entity()
        scene.replace(t, "Transform", { pos = { 0, 0.07, 0 } })
        scene.replace(t, "Renderable", { geom = tip, noShadow = true })
        scene.replace(t, "MaterialRef", { material = matAxis })
        scene.add_child(j, t)
    end
end

function M._animate(self)
    yaw("basegen8/head", 90 * math.pi / 180)
end
return M
