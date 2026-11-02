-- Post-fix verification on the full Hashimoto character. Re-runs the two original bug cases
-- (neckUpper 25 deg, head 25 deg) plus an unambiguous head 90 deg, with thin green axis arrows
-- on every joint so the skinned mesh and the bone direction are directly comparable.
-- Phases are timed by scene.time() (load takes 10-60 s, so capture off the "dbg t=" log lines):
--   t <  8 : bind pose
--   t < 16 : neckUpper yawed 25 deg (bone-local Y, post-multiplied)
--   t < 24 : head yawed 25 deg, neck back to bind
--   t >= 24: head yawed 90 deg (profile), neck back to bind
local M = {}
local bind = {}

local function bindPose(name)
    if not bind[name] then
        local p = scene.get_pose(name)
        bind[name] = { p.pos, p.quat }
    end
    return bind[name]
end

local function yaw(name, ang)
    local b = bindPose(name)
    if ang == 0 then
        scene.set_pose(name, b[1], b[2])
    else
        scene.set_pose(name, b[1], math.qmul(b[2], math.qaxis({ 0, 1, 0 }, ang)))
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
    graphics.gltf(ch, "local/hashimoto.glb")

    local matAxis = scene.entity("mat_axis")
    scene.replace(matAxis, "Material", { baseColor = {0, 1, 0.4}, metallic = 0, roughness = 0.4,
                                         emissive = {0, 1, 0.4} })
    local shaft = geom(mesh.box({ 0.004, 0.07, 0.004 }))
    local tip = geom(mesh.sphere({ subdiv = 1, radius = 0.008 }))
    local skel = scene.find_components("Skeleton", ch)[1]
    for _, j in ipairs(scene.component(skel, "Skeleton").joints) do
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
    local t = scene.time()
    local neck, head = 0, 0
    if t >= 8 and t < 16 then
        neck = 25 * math.pi / 180
    elseif t >= 16 and t < 24 then
        head = 25 * math.pi / 180
    elseif t >= 24 then
        head = 90 * math.pi / 180
    end
    yaw("hashimoto/neckUpper", neck)
    yaw("hashimoto/head", head)
end
return M
