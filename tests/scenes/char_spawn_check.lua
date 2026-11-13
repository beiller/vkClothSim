-- char_spawn_check: the character rig must be spawned by char_ctrl.setup() itself (no char_* objects
-- in the JSON). Verifies spawn, walk, jump, landing, and momentum carry-through. dt = 1/120 per frame.
local cc = require("scenes/char_ctrl")

local tick = 0
local ents
local yMax = -1e9

function _on_ready(self)
    ents = cc.setup() -- no rb/ghost: the module must spawn the rig itself
end

function _physics(self, dt)
    tick = tick + 1
    local dir, jump = { 0, 0 }, false
    if tick <= 240 then
        dir = { 0, -1 } -- keep walking -Z through the whole sequence (momentum source)
    end
    if tick == 130 then
        jump = true
    end
    local r = cc.update(dt, dir, jump)
    local p = scene.get_pose(ents.rb).pos
    if p[2] > yMax then
        yMax = p[2]
    end

    if tick == 120 then
        t.check("spawn: rig entities exist", ents.rb ~= nil and ents.ghost ~= nil)
        t.eq("spawn: on ground", p[2], 0.8, 0.15)
        t.check("spawn: walked -Z", p[3] < -1.5)
    end
    if tick == 360 then -- kJumpVel=6 => ~1.2 s arc, lands ~tick 277
        t.check("spawn: jumped (yMax > 2.0)", yMax > 2.0)
        t.eq("spawn: landed on ground", p[2], 0.8, 0.15)
        t.check("spawn: grounded after landing", r.grounded)
        t.check("spawn: momentum carried -Z", p[3] < -3.0)
        t.done()
    end
end
