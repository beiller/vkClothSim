-- Verify AnimationClip/AnimationPlayer via plain scene.emplace (no glTF): one track
-- animates Transform.pos of a spawned entity; the sampled value must track player time.
local playerE

function _on_ready()
    local target = scene.entity("anim_target")
    scene.emplace(target, "Transform", { pos = { 0, 0, 0 } })
    local clip = scene.entity()
    scene.emplace(clip, "AnimationClip", {
        targets = { "anim_target.Transform.pos" },
        trackStart = { 0, 2 },
        times = { 0, 1 },
        values = { 0, 0, 0, 2, 0, 0 },
    })
    playerE = scene.entity()
    scene.emplace(playerE, "AnimationPlayer", { clip = clip, playing = 1 })
end

local done_ = false
function _animate(dt)
    if done_ then return end
    local p = scene.component(playerE, "AnimationPlayer")
    if not p or not p.time then return end
    if p.time < 0.49 or p.time > 0.51 then return end
    local pose = scene.get_pose("anim_target")
    t.eq("player time window", p.time, 0.5, 0.02)
    t.eq("pos x tracks time (2*t)", pose.pos[1], 2 * p.time, 1e-3)
    t.eq("pos y stays 0", pose.pos[2], 0.0, 1e-6)
    done_ = true
    t.done()
end
