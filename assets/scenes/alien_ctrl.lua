-- alien ctrl: WASD/Shift/Space character controller over the char_ctrl module; fly camera from scenes/camera.
local cc = require("scenes/char_ctrl")

local W, A, S_, D, SHIFT, SPACE = 87, 65, 83, 68, 340, 32
local kbH
local animState
local rbH -- capsule entity (spawned by char_ctrl.setup)

function _on_ready(self)
    local ents = cc.setup({ alien = scene.get_entity("alien") }) -- spawns the character rig itself
    rbH = ents.rb
    -- ground visuals (its physics body is declarative in the JSON)
    local g = scene.get_entity("ground_geom")
    scene.replace(g, "Geometry", mesh.box({ 20, 1, 20 }))
    local ground = scene.get_entity("ground")
    scene.replace(ground, "Renderable", { geom = g })
    scene.replace(ground, "MaterialRef", { material = scene.get_entity("mat_ground") })
end

local function setAnim(state)
    if state == animState then
        return
    end
    animState = state
    if state == "run" then
        scene.set("alien-grey-rigged/run.AnimationPlayer.playing", 1)
        scene.set("alien-grey-rigged/walk.AnimationPlayer.playing", 0)
    elseif state == "walk" then
        scene.set("alien-grey-rigged/walk.AnimationPlayer.playing", 1)
        scene.set("alien-grey-rigged/run.AnimationPlayer.playing", 0)
    else -- idle: hold the last pose
        scene.set("alien-grey-rigged/walk.AnimationPlayer.playing", 0)
        scene.set("alien-grey-rigged/run.AnimationPlayer.playing", 0)
    end
end

function _physics(self, dt)
    if not kbH then
        kbH = scene.get_entity("keyboard")
    end
    local dx, dz = 0, 0
    if input.key_down(kbH, W) then dz = dz - 1 end
    if input.key_down(kbH, S_) then dz = dz + 1 end
    if input.key_down(kbH, A) then dx = dx - 1 end
    if input.key_down(kbH, D) then dx = dx + 1 end
    local r = cc.update(dt, { dx, dz }, input.key_down(kbH, SPACE), input.key_down(kbH, SHIFT))

    setAnim(r.speed < 0.3 and "idle" or (r.speed > 3.0 and "run" or "walk"))
end


function _animate(self, dt)
    local cam = scene.get_entity("cam")
    local alien_pos = scene.component(rbH, "Transform").pos
    scene.component(cam, "Transform").pos = {alien_pos[1], alien_pos[2] + 2.6, alien_pos[3] + 5.0}
end