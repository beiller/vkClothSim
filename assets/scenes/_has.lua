-- Hashimoto idle: subtle breathing/sway/look-around + one hand wave, on top of the file's
-- frame-0 shape (static morph weights applied live by the GPU, FHMHashimoto pushed to 0.85).
local M = {}
local bind = {}

-- capture a joint's bind-pose local TRS once, then compose the idle rotation onto it
local function bindPose(name)
    if not bind[name] then
        local p = scene.get_pose(name)
        bind[name] = { p[1], p[2] }
    end
    return bind[name]
end

local function sway(name, q)
    local b = bindPose(name)
    -- post-multiply: rotate in the bone's own local frame (its local Y is the motion axis);
    -- pre-multiplying would rotate in the parent's frame (e.g. the neck's tilted axis)
    scene.set_pose(name, b[1], scene.qmul(b[2], q))
end

local function geom(t)
    local g = scene.entity()
    scene.replace(g, "Geometry", t)
    return g
end

function M._on_ready(self)
    local mat = scene.entity("mat_ground")
    scene.replace(mat, "Material", { baseColor = {0.35, 0.35, 0.38}, metallic = 0, roughness = 0.95 })
    local ground = scene.entity("ground")
    scene.replace(ground, "Transform", { pos = { 0, -1, 0 } })
    scene.replace(ground, "Renderable", { geom = geom(mesh.box({ 20, 1, 20 })) })
    scene.replace(ground, "MaterialRef", { material = mat })

    local ch = scene.entity("char")
    scene.replace(ch, "Transform", { pos = { 0, 0, 0 } })
    render.gltf(ch, "local/hashimoto.glb")
    scene.set_morph(ch, "FHMHashimoto", 0.85) -- entity handle: the name "char" is ambiguous
end

function M._animate(self)
    local t = scene.time()
    -- breathing: chest pitch, slow
    sway("hashimoto/chestUpper", scene.qaxis({ 1, 0, 0 }, 0.03 * math.sin(t * 1.1)))
    -- hip sway: yaw + a slow roll
    sway("hashimoto/hip",
         scene.qmul(scene.qaxis({ 0, 1, 0 }, 0.07 * math.sin(t * 0.7)),
                    scene.qaxis({ 1, 0, 0 }, 0.02 * math.sin(t * 0.5))))
    -- head look-around: two incommensurate sines
    local hy = 0.16 * math.sin(t * 0.53) + 0.05 * math.sin(t * 1.31)
    local hp = 0.04 * math.sin(t * 0.37)
    sway("hashimoto/head",
         scene.qmul(scene.qaxis({ 0, 1, 0 }, hy),
                    scene.qaxis({ 1, 0, 0 }, hp)))
    -- gentle right-hand wave
    sway("hashimoto/rForearmBend", scene.qaxis({ 1, 0, 0 }, 0.18 * math.sin(t * 1.7)))
    sway("hashimoto/rHand", scene.qaxis({ 0, 0, 1 }, 0.15 * math.sin(t * 1.7 + 0.6)))
end
return M
