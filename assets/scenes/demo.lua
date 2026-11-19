-- demo behavior: scattered rigid capsules + cloth/ball soft bodies + ground
-- (mesh is the engine-loaded geometry stdlib global; lights/materials/UI live in demo.json)


local clothH
local ball

local world_scale = 0.666

-- stdlib geometry tables are the Geometry struct itself: stash one in a geometry entity
-- (the renderer uploads it lazily on first sight)
local function geom(t)
    local g = scene.entity()
    scene.replace(g, "Geometry", t)
    return g
end

function _on_ready(self)
    -- ground: one entity with a visual box + static box collider; the soft sim's floor is explicit
    local ground = scene.entity("ground")
    scene.replace(ground, "Transform", { pos = { 0, -1, 0 } })
    scene.replace(ground, "Renderable", { geom = geom(mesh.box({ 55, 1, 55 })) })
    scene.replace(ground, "MaterialRef", { material = scene.get_entity("mat_ground") })
    local gShape = scene.entity()
    scene.replace(gShape, "BoxShape", { half = { 40, 1, 40 } })
    local gCol = scene.entity()
    scene.replace(gCol, "Collider", { shape = gShape })
    scene.replace(ground, "KinematicBody", { colliders = { gCol } })
    soft.set_floor(0.0001)

    -- 50 scattered dynamic capsules, all sharing one shape, one material, one geometry
    local cap_length = 0.9 * world_scale
    local cap_r = 0.5 * world_scale
    local capShape = scene.entity()
    scene.replace(capShape, "CapsuleShape", { halfLen = cap_length, radius = cap_r })
    local capCol = scene.entity()
    scene.replace(capCol, "Collider", { shape = capShape })
    local capGeom = geom(mesh.capsule({ halfLen = cap_length, radius = cap_r }))
    local matCapsule = scene.get_entity("mat_capsule")
    math.randomseed(12345)
    for i = 1, 50 do
        local px = (math.random() * 2 - 1) * 2 * world_scale
        local py = 4.5 + 3.5 * math.random()
        local pz = (math.random() * 2 - 1) * 2 * world_scale
        local q = math.qmul(math.qaxis({0, 1, 0}, math.random() * 2 * math.pi),
                              math.qaxis({1, 0, 0}, (math.random() * 2 - 1) * 0.9))
        local e = scene.entity()
        scene.replace(e, "Transform", { pos = {px, py, pz}, quat = q })
        scene.replace(e, "Renderable", { geom = capGeom })
        scene.replace(e, "MaterialRef", { material = matCapsule })
        scene.replace(e, "RigidBody", { colliders = { capCol } })
    end

    -- soft bodies; the cloth's top row is pinned (per-vertex, weight 1) to its spawn pose, the rest hangs free
    local clothSpan = 8.0 * world_scale
    local clothDim = 64
    local clothGeom = geom(mesh.plane({ cw = clothDim, ch = clothDim, span = clothSpan }))
    clothH = scene.entity("cloth")
    scene.replace(clothH, "Transform", { pos = {0, 10, 0} })
    scene.replace(clothH, "Renderable", { geom = clothGeom })
    scene.replace(clothH, "MaterialRef", { material = scene.get_entity("mat_cloth") })
    scene.replace(clothH, "SoftBodyData",
                  { cons = mesh.planeCons({ cw = clothDim, ch = clothDim, span = clothSpan }), mass = 1.0 })
    -- pin the front edge (plane row gy=0 sits at z=-span/2) where it spawns; set_pins captures current positions
    local pinW = {}
    for i = 1, clothDim * clothDim do
        pinW[i] = 0.0
    end
    for i = 1, clothDim do
        pinW[i] = 1.0
    end
    soft.set_pins(clothH, pinW)

    local ballT = mesh.sphere({ subdiv = 3, radius = 1.5 * world_scale })
    ball = scene.entity("ball")
    scene.replace(ball, "Transform", { pos = {0, 12, 0} })
    scene.replace(ball, "Renderable", { geom = geom(ballT) })
    scene.replace(ball, "MaterialRef", { material = scene.get_entity("mat_ball") })
    scene.replace(ball, "SoftBodyData", { cons = mesh.ballCons(ballT), mass = 2.0 })

    -- tapered-capsule + sphere rigid bodies: exercise the GPU cloth's tapered/sphere colliders
    local tcH = 1.2 * world_scale
    local tcRB = 0.55 * world_scale
    local tcRT = 0.18 * world_scale
    local tcShape = scene.entity()
    scene.replace(tcShape, "TaperedCapsule", { halfHeight = tcH, bottomRadius = tcRB, topRadius = tcRT })
    local tcCol = scene.entity()
    scene.replace(tcCol, "Collider", { shape = tcShape })
    local tcBody = scene.entity("taper")
    scene.replace(tcBody, "Transform", { pos = {-1.6 * world_scale, 9, 0}, quat = math.qaxis({1, 0, 0}, -0.25) })
    scene.replace(tcBody, "Renderable",
                  { geom = geom(mesh.taperedCapsule({ halfHeight = tcH, bottomRadius = tcRB, topRadius = tcRT })) })
    scene.replace(tcBody, "MaterialRef", { material = matCapsule })
    scene.replace(tcBody, "RigidBody", { colliders = { tcCol } })

    local spR = 0.6 * world_scale
    local spShape = scene.entity()
    scene.replace(spShape, "SphereShape", { radius = spR })
    local spCol = scene.entity()
    scene.replace(spCol, "Collider", { shape = spShape })
    local spBody = scene.entity("sphere")
    scene.replace(spBody, "Transform", { pos = {1.6 * world_scale, 9, 0} })
    scene.replace(spBody, "Renderable", { geom = geom(mesh.sphere({ subdiv = 3, radius = spR })) })
    scene.replace(spBody, "MaterialRef", { material = matCapsule })
    scene.replace(spBody, "RigidBody", { colliders = { spCol } })

    -- glft tests
    local m = scene.entity("model")
    scene.replace(m, "Transform", { pos = { 3, 0, 0 } })
    graphics.gltf(m, "scenes/models/cube_test.glb")
end

-- struct defaults, as the JSON "reset params" button expects (demo.LuaScript.reset_params)
function reset_params()
    for _, h in ipairs({ clothH, ball }) do
        scene.set(h, "SoftBodyData.mass", 1.0)
        scene.set(h, "SoftBodyData.damping", 0.96)
        scene.set(h, "SoftBodyData.passes", 8)
        scene.set(h, "SoftBodyData.stiffness", 1.0)
        scene.set(h, "SoftBodyData.tension", 1.0)
        scene.set(h, "SoftBodyData.friction", 2.0)
        scene.set(h, "SoftBodyData.steps", 3)
    end
end


local dtAccum = 0.0
function _animate(self, dt)
    dtAccum = dtAccum + dt
    if dtAccum > 1.0 then
        print(engine.performance_metrics())
        dtAccum = 0.0
    end
end