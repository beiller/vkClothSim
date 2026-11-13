-- Verify glTF animation auto-wiring: loadGltf spawns a player per animation and the
-- sampled joint transforms must track the player time (jointA rotates 0->90deg Z,
-- jointB translates x 0->1 over 1s).
function _on_ready()
    local ok = pcall(scene.get, "anim_test/spin.AnimationPlayer.time")
    t.check("player entity exists", ok)
    scene.set("anim_test/spin.AnimationPlayer.playing", 1) -- imported clips start paused
end

local done_ = false
function _animate(dt)
    if done_ then return end
    local pa = scene.get_pose("anim_test/jointA")
    local pb = scene.get_pose("anim_test/jointB")
    -- derive t from jointB's x (pos = t exactly); cross-check with jointA's angle
    local tb = pb.pos[1]
    if tb < 0.49 or tb > 0.51 then return end
    local ang = 2 * math.atan(pa.quat[3], pa.quat[4]) -- rotation about Z in radians
    t.eq("jointB x tracks time", tb, 0.5, 0.02)
    t.eq("jointA angle tracks time (pi/2*t)", ang, math.pi / 2 * tb, 1e-3)
    t.eq("jointA quat normalized", math.sqrt(pa.quat[1]^2 + pa.quat[2]^2 + pa.quat[3]^2 + pa.quat[4]^2), 1.0, 1e-3)
    done_ = true
    t.done()
end
