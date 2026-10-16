-- Bone axis probe: a thin green arrow along each bone's local +Y (shaft + tip), parented to the
-- joint entity so it moves and turns with the bone. Character mesh hidden - axes only.
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

function M._on_ready(self)
    render.material("ground", {0.35, 0.35, 0.38}, 0, 0.95)
    render.mesh(scene.entity("ground"), render.geometry(mesh.box({ 20, 1, 20 })), "ground", { 0, -1, 0 })

    local ch = scene.entity("char")
    scene.replace(ch, "Transform", { pos = { 0, 0, 0 } })
    render.gltf(ch, "local/basegen8.glb")

    -- set to true to hide the mesh (axes only)
    local hideMesh = false
    for _, n in ipairs({ "basegen8/Mesh", "basegen8/Genesis8FemaleEyelashes" }) do
        scene.visible(n, not hideMesh)
    end

    render.material("axis", {0, 1, 0.4}, 0, 0.4, nil, {0, 1, 0.4})
    local shaft = render.geometry(mesh.box({ 0.004, 0.07, 0.004 }))
    local tip = render.geometry(mesh.sphere({ subdiv = 1, radius = 0.008 }))
    for _, j in ipairs(scene.joints(ch)) do
        local s = scene.entity()
        render.mesh(s, shaft, "axis", { 0, 0.035, 0 }, nil, { parent = j, noShadow = true })
        local t = scene.entity()
        render.mesh(t, tip, "axis", { 0, 0.07, 0 }, nil, { parent = j, noShadow = true })
    end
end

function M._animate(self)
    yaw("basegen8/head", 90 * math.pi / 180)
end
return M
