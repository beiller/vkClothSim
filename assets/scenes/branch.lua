-- a tree branch: sways its own kinematic capsule in the shared sin-wave breeze.
-- attached per-branch by the tree builder via scene.attach_script with params
-- {pos, quat, amp, phase, isClock}; pos/quat are the local (parent-relative) pose.
local M = {}

-- shared breeze clock (module-level, so every branch instance shares it); the trunk (isClock)
-- is the only instance that advances it
local windTime = 0.0
local windAxis = { 0, 0, 1 } -- local-frame bend axis
local windFreq = 1.0

function M._on_ready(self)
    local p = self.params
    self.basePos = p.pos
    self.baseQuat = p.quat
    self.amp = p.amp
    self.phase = p.phase
end

function M._animate(self, dt)
    if self.params.isClock then
        windTime = windTime + dt
    end
    local sway = scene.qaxis(windAxis, self.amp * math.sin(windFreq * windTime + self.phase))
    scene.replace(self.entity, "Transform", { pos = scene.qrot(sway, self.basePos), quat = scene.qmul(sway, self.baseQuat) })
end

return M
