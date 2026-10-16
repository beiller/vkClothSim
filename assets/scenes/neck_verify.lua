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
    render.gltf(ch, "local/hashimoto.glb")

    for _, n in ipairs({ "hashimoto/Genesis8FemaleEyelashes", "hashimoto/HeadLight_R_608",
                          "hashimoto/HeadLight_L_608", "hashimoto/GoldenPalace_2254", "hashimoto/Base_98721" }) do
        scene.visible(n, false)
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
