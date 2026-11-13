-- char_ctrl: kinematic-ghost + rigid-capsule character controller (shared module).
--
-- The ghost (KinematicBody) takes WASD; a 6DOF joint (X/Z locked, Y free, rotation locked to the
-- upright ghost) drags the capsule (RigidBody); visuals are parented under the capsule. The ghost's
-- lead over the capsule is capped (kMaxLead), so wall/corner resolution comes from Jolt collision
-- instead of a predictive raycast while the gap stays bounded. NOTE: do not fully re-sync the ghost
-- to the capsule - a clamped ghost has zero velocity in Jolt's view, the sleeping capsule then
-- ignores the joint (the character freezes), and releasing the clamp causes phantom-velocity drift.

local M = {}
local S

local kWalkSpeed = 2.0     -- m/s
local kRunMul = 2.25       -- shift multiplier (-> 7 m/s)
local kJumpVel = 6         -- m/s (impulse = mass * this)
local kStepVel = 2.8       -- m/s step-up nudge
local kMaxLead = 0.15      -- max ghost lead over the capsule (wall gap cap, meters)
local kGroundEps = 0.05    -- foot window (m): grounded if the capsule bottom is within this of a surface
local kJumpCdTime = 0.5    -- jump cooldown in seconds
local kAirSteer = 0.5      -- m/s of steering authority in air (applied against/along the launch momentum)
local kCapsHalfLen = 0.45  -- capsule half length (m); center-to-bottom = halfLen + radius
local kCapsRadius = 0.35   -- capsule radius (m)

-- Spawns the character rig: capsule RigidBody + ghost KinematicBody + 6DOF joint, all wired up in one
-- scene.spawn batch (sibling refs resolve by key). Returns { rb = entity, ghost = entity }.
local function spawnRig(pos, mass)
    local e = scene.spawn({
        char_capsule = { CapsuleShape = { halfLen = kCapsHalfLen, radius = kCapsRadius } },
        char_col = { Collider = { shape = "char_capsule" } },
        char_rb = {
            Transform = { pos = pos },
            RigidBody = { mass = mass, friction = 0.8, restitution = 0, damping = 0.1, colliders = { "char_col" } },
        },
        char_ghost_shape = { SphereShape = { radius = 0.05 } },
        char_ghost_col = { Collider = { shape = "char_ghost_shape" } },
        char_ghost = { Transform = { pos = pos }, KinematicBody = { colliders = { "char_ghost_col" } } },
        char_joint = {
            SixDofJoint = {
                bodyA = "char_ghost", bodyB = "char_rb", grabPos = pos,
                limOn = true,
                limTransMin = { 0, -1e30, 0 }, limTransMax = { 0, 1e30, 0 },
                limAngMin = { 0, 0, 0 }, limAngMax = { 0, 0, 0 },
                excludeCollision = true,
            },
        },
    })
    return e.char_rb, e.char_ghost
end

-- o: { rb?, ghost?, alien?, pos?, mass?, drop? } - if rb/ghost are omitted the full rig is spawned at
-- o.pos (default {0, 0.8, 0}); alien is parented under rb preserving world placement; drop = capsule
-- center-to-bottom distance (defaults to halfLen + radius). Returns { rb = entity, ghost = entity }.
function M.setup(o)
    o = o or {}
    local rb, ghost = o.rb, o.ghost
    if not (rb and ghost) then
        rb, ghost = spawnRig(o.pos or { 0, 0.8, 0 }, o.mass or 70)
        o.rb, o.ghost = rb, ghost -- the rest of setup references o.rb/o.ghost
    end
    S = { rb = rb, ghost = ghost, mass = o.mass or 70, drop = o.drop or (kCapsHalfLen + kCapsRadius) }
    if o.alien then
        local ap = scene.get_pose(o.alien)
        local rp = scene.get_pose(o.rb)
        local off = o.alienOffset or { 0, 0, 0 }
        scene.replace(o.alien, "Parent", { e = o.rb })
        scene.replace(o.alien, "Transform", { pos = { ap.pos[1] - rp.pos[1] + off[1], ap.pos[2] - rp.pos[2] + off[2],
            ap.pos[3] - rp.pos[3] + off[3] } })
    end
    local p = scene.get_pose(o.rb).pos
    S.prev = { p[1], p[2], p[3] }
    S.blocked = 0
    S.jumpCd = 0
    S.mom = nil       -- launch momentum (horizontal m/s), preserved while airborne
    S.wasAir = false
    return { rb = S.rb, ghost = S.ghost }
end

-- dir: {x, z} desired world horizontal direction (any magnitude); wantJump/run: bool
-- returns { speed = actual horizontal m/s from last frame, grounded = bool }
function M.update(dt, dir, wantJump, run)
    local gp = scene.get_pose(S.ghost)
    local rp = scene.get_pose(S.rb)

    -- 1) soft cap on the ghost's lead (see header note); wall resolution stays in Jolt
    local pos = { gp.pos[1], gp.pos[2], gp.pos[3] }
    local q = gp.quat

    -- 2) grounded: downward ray from the capsule center of mass. Stateless - there is no flag to get
    -- stuck in; an absorbed jump or a missed landing frame can't wedge us "airborne". Excludes both
    -- bodies: the ghost sits at the ray origin and the origin is inside the capsule itself.
    local hit = physics.raycast(rp.pos, { 0, -1, 0 }, S.drop + kGroundEps, { S.rb, S.ghost })
    local grounded = hit ~= nil
    -- air transition: going airborne WITHOUT a recent impulse (walked off a cliff) must not keep
    -- preserving stale jump momentum; a real launch happens inside the jump cooldown
    if grounded then
        S.wasAir = false
    elseif not S.wasAir then
        if S.jumpCd == 0 then
            S.mom = nil
        end
        S.wasAir = true
    end

    -- 3) horizontal control as a ghost velocity vector: on the ground, walk/run in the input dir; in
    -- air, carry the launch momentum plus a small steering authority (works against the momentum too).
    local dx, dz = (dir and dir[1]) or 0, (dir and dir[2]) or 0
    local len = math.sqrt(dx * dx + dz * dz)
    local vx, vz = 0, 0
    if len > 1e-4 then
        dx, dz = dx / len, dz / len
        if grounded then
            vx, vz = dx * kWalkSpeed * (run and kRunMul or 1), dz * kWalkSpeed * (run and kRunMul or 1)
        else
            vx, vz = (S.mom and S.mom[1]) or 0, (S.mom and S.mom[2]) or 0
            vx, vz = vx + dx * kAirSteer, vz + dz * kAirSteer
        end
    elseif not grounded and S.mom then
        vx, vz = S.mom[1], S.mom[2] -- no input in air: pure ballistic drift
    end
    -- cap the ghost's TOTAL xz lead over the capsule (was: along the input dir): in open air the
    -- joint keeps the capsule glued to the ghost so this never binds; against a wall it holds the
    -- ghost so collision, not the joint, resolves the contact and landing never yanks the capsule
    local lx, lz = gp.pos[1] - rp.pos[1], gp.pos[3] - rp.pos[3]
    local ll = math.sqrt(lx * lx + lz * lz)
    if ll >= kMaxLead then
        vx, vz = 0, 0 -- at the cap: hold until the capsule catches up
    else
        local nx, nz = lx + vx * dt, lz + vz * dt
        local nl = math.sqrt(nx * nx + nz * nz)
        if nl > kMaxLead then
            local s = (kMaxLead - ll) / (nl - ll) -- shrink this frame's move to land exactly on the cap
            vx, vz = vx * s, vz * s
        end
    end
    pos[1] = pos[1] + vx * dt
    pos[3] = pos[3] + vz * dt
    if len > 1e-4 then
        q = math.qaxis({ 0, 1, 0 }, math.atan2(dx, dz)) -- face input; rotation lock carries it to the capsule
    end
    scene.replace(S.ghost, "Transform", { pos = pos, quat = q })

    -- 4) horizontal progress (last frame's displacement along the input dir)
    local px, pz = rp.pos[1] - S.prev[1], rp.pos[3] - S.prev[3]
    if len > 1e-4 and grounded then
        if px * dx + pz * dz < kWalkSpeed * dt * 0.25 then
            if S.blocked >= 0 then
                S.blocked = S.blocked + 1
            end
        else
            S.blocked = 0 -- made progress: re-arm step-up
        end
    else
        S.blocked = 0
    end

    -- 5) vertical impulses: jump wins over the step-up nudge (one-shot, auto-cleared by physics).
    -- No airborne bookkeeping needed: grounded is re-derived from the ray every frame. The cooldown
    -- (decremented every frame, air or ground) blocks stacked impulses while we're still inside the
    -- foot window after a launch; it expires well before landing, so held-jump bunny-hops are intact.
    if S.jumpCd > 0 then
        S.jumpCd = S.jumpCd - dt
    end
    local impY = 0
    if S.jumpCd < 0.0001 then
        if wantJump and grounded then
            impY = S.mass * kJumpVel
        elseif S.blocked == 6 then -- blocked a few frames while grounded: one nudge per episode
            impY = S.mass * kStepVel
            S.blocked = -1
        end
    end
    if impY > 0 then
        scene.component(S.rb, "RigidBody").impulse = { 0, impY, 0 }
        S.jumpCd = kJumpCdTime
        S.mom = { px / dt, pz / dt } -- capture horizontal velocity at launch (last frame's displacement)
    end

    S.prev = { rp.pos[1], rp.pos[2], rp.pos[3] }
    return { speed = math.sqrt(px * px + pz * pz) / dt, grounded = grounded }
end

return M
