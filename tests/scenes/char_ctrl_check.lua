-- char_ctrl headless check: setup() spawns the rig itself (no char_* objects in the JSON); walk,
-- momentum jump, step-up riser (must not stick), wall (no pass-through / no push-back), ghost
-- re-sync, back off. Headless runs one 240 Hz physics tick per frame (walk 2 m/s, jump v=6 => ~294-tick arc).
-- NOTE: the level is deliberately deep in Z - the module's airborne momentum capture is noisy
-- (pose-delta based), so a launched character can drift several meters off its launch line.
local cc = require("scenes/char_ctrl")

local tick = 0
local ents
local yMax = -1e9

function _on_ready(self)
    ents = cc.setup() -- no rb/ghost: the module must spawn the rig itself at {0, 0.8, 0}
end

local function p()
    return scene.get_pose(ents.rb).pos
end

function _physics(self, dt)
    tick = tick + 1
    local dir, jump = { 0, 0 }, false
    if tick <= 700 then
        dir = { 0, -1 } -- walk forward (-Z); momentum jump at tick 400 (lands ~694)
        if tick == 400 then
            jump = true
        end
    elseif tick <= 1900 then
        dir = { 1, 0 } -- +X: over the riser (step-up), into the wall at x=4.5
    elseif tick <= 2350 then
        dir = { -1, 0 } -- back off
    end
    local r = cc.update(dt, dir, jump)
    local pos = p()
    if pos[2] > yMax then
        yMax = pos[2]
    end

    if tick == 5 then
        t.check("spawn: rig entities exist", ents.rb ~= nil and ents.ghost ~= nil)
        t.check("spawn: char_rb in registry", scene.get_entity("char_rb") ~= nil)
    end
    if tick == 550 then -- mid-flight of the momentum jump (launch 400, lands ~694)
        t.check("jump: airborne mid-ascent", not r.grounded)
    end
    if tick == 710 then -- just after landing, walking freely again
        t.check("momentum: carried -Z through the jump", pos[3] < -3.0)
        t.eq("landed on ground", pos[2], 0.8, 0.15)
        t.check("grounded after landing", r.grounded)
        t.check("jump apex > 2.0", yMax > 2.0)
        local gp = scene.get_pose(ents.ghost).pos
        t.check("resync: ghost xz tracks capsule", math.abs(gp[1] - pos[1]) < 0.1 and math.abs(gp[3] - pos[3]) < 0.1)
    end
    if tick == 1700 then -- held against the wall (contact ~tick 1480; phase B runs to 1900)
        t.check("wall: no pass-through", pos[1] < 4.35)
        t.check("wall: reached, not stuck at riser", pos[1] > 3.8)
        t.check("wall: grounded while held", r.grounded)
    end
    if tick == 2350 then
        t.check("back off: moved away cleanly", pos[1] < 3.0 and pos[1] > -0.5)
        t.eq("back off: still on riser", pos[2], 1.05, 0.15)
    end
    if tick == 2370 then
        print(string.format("CHAR_CTRL final x=%.2f y=%.2f z=%.2f yMax=%.2f", p()[1], p()[2], p()[3], yMax))
        t.done()
    end
end
