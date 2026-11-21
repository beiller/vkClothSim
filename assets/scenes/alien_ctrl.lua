-- alien ctrl: WASD/Shift/Space character controller over the char_ctrl module; fly camera from scenes/camera.
local cc = require("scenes/char_ctrl")

local W, A, S_, D, SHIFT, SPACE = 87, 65, 83, 68, 340, 32
local kbH
local animState
local rbH -- capsule entity (spawned by char_ctrl.setup)

-- run/walk crossfade: both players play while either has weight; strengths cross over FADE seconds so
-- the blended pose (relative weights, sum ~1) moves continuously between clips - ANIMATION_BLEND_PLAN.md
local FADE = 0.25
local RUN_P, WALK_P = "alien-grey-rigged/run.AnimationPlayer", "alien-grey-rigged/walk.AnimationPlayer"
local RUN_D, WALK_D = "alien-grey-rigged/run_clip.AnimationClip.duration",
    "alien-grey-rigged/walk_clip.AnimationClip.duration"
local runW, walkW = 0, 0 -- current strengths (source of truth)
local fromRun, fromWalk, tgtRun, tgtWalk = 0, 0, 0, 0
local fadeT, fading = 0, false
local speedSm = 0 -- low-passed speed for state selection: per-tick r.speed oscillates (joint catch-up at
                  -- 240Hz), and unfiltered it flaps the idle/walk/run state every tick

-- map `fromP`'s gait phase onto `toP`'s clock (crude cycle mapping - the clips have different keyframe
-- counts, so this aligns stride fraction, not pose). Without it a clip resuming from a stopped clock
-- would mix at a random relative phase and the crossfade reads as two unrelated poses.
local function phaseAlign(fromP, toP, fromDurUrl, toDurUrl)
    local fd, td = scene.get(fromDurUrl), scene.get(toDurUrl)
    if fd > 0 and td > 0 then
        scene.set(toP .. ".time", (scene.get(fromP .. ".time") / fd) % 1 * td)
    end
end

local function setAnim(state)
    if state == animState then
        return
    end
    animState = state
    if state == "run" then
        tgtRun, tgtWalk = 1, 0
    elseif state == "walk" then
        tgtRun, tgtWalk = 0, 1
    else -- idle: fade both out to hold the last pose
        tgtRun, tgtWalk = 0, 0
    end
    fromRun, fromWalk = runW, walkW -- resume from wherever a previous fade left off
    fadeT = 0
    fading = (fromRun ~= tgtRun) or (fromWalk ~= tgtWalk)
    -- phase-match a clip resuming from a stopped clock to the other clip's gait phase
    if state == "run" and runW <= 0 then
        phaseAlign(WALK_P, RUN_P, WALK_D, RUN_D)
    elseif state == "walk" and walkW <= 0 then
        phaseAlign(RUN_P, WALK_P, RUN_D, WALK_D)
    end
    scene.set(RUN_P .. ".playing", (tgtRun + runW) > 0 and 1 or 0)
    scene.set(WALK_P .. ".playing", (tgtWalk + walkW) > 0 and 1 or 0)
end

local function tickFade(dt)
    if not fading then
        return
    end
    fadeT = fadeT + dt
    local f = math.min(fadeT / FADE, 1.0)
    runW = fromRun + (tgtRun - fromRun) * f
    walkW = fromWalk + (tgtWalk - fromWalk) * f
    scene.set(RUN_P .. ".strength", runW)
    scene.set(WALK_P .. ".strength", walkW)
    if f >= 1.0 then
        fading = false
        -- stop the player that faded out so its clock no longer advances
        scene.set(RUN_P .. ".playing", tgtRun > 0 and 1 or 0)
        scene.set(WALK_P .. ".playing", tgtWalk > 0 and 1 or 0)
    end
end

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

    -- low-pass the speed (tau ~0.1s) so a single noisy tick can't flap the anim state
    speedSm = speedSm + (r.speed - speedSm) * math.min(1, dt / 0.1)
    setAnim(speedSm < 0.3 and "idle" or (speedSm > 3.0 and "run" or "walk"))
    tickFade(dt)
end


function _animate(self, dt)
    local cam = scene.get_entity("cam")
    local alien_pos = scene.component(rbH, "Transform").pos
    scene.component(cam, "Transform").pos = {alien_pos[1], alien_pos[2] + 2.6, alien_pos[3] + 5.0}
end