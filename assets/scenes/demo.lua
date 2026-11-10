-- demo behavior: scattered rigid capsules + cloth/ball soft bodies + ground
-- (mesh is the engine-loaded geometry stdlib global; lights/materials/UI live in demo.json)


local clothH
local ball
local pinTime = 0.0
local released = false
local PIN_SECS = 3.0

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

    -- soft bodies; the entity's Transform is the pinned (spawn) pose, the cloth is released after PIN_SECS
    local clothSpan = 8.0 * world_scale
    local clothDim = 64
    local clothGeom = geom(mesh.plane({ cw = clothDim, ch = clothDim, span = clothSpan }))
    clothH = scene.entity("cloth")
    scene.replace(clothH, "Transform", { pos = {0, 10, 0} })
    scene.replace(clothH, "Renderable", { geom = clothGeom })
    scene.replace(clothH, "MaterialRef", { material = scene.get_entity("mat_cloth") })
    scene.replace(clothH, "SoftBodyData",
                  { cons = mesh.planeCons({ cw = clothDim, ch = clothDim, span = clothSpan }),
                    pinned = 1, mass = 1.0 })

    local ballT = mesh.sphere({ subdiv = 3, radius = 1.5 * world_scale })
    ball = scene.entity("ball")
    scene.replace(ball, "Transform", { pos = {0, 12, 0} })
    scene.replace(ball, "Renderable", { geom = geom(ballT) })
    scene.replace(ball, "MaterialRef", { material = scene.get_entity("mat_ball") })
    scene.replace(ball, "SoftBodyData", { cons = mesh.ballCons(ballT), mass = 2.0 })

    -- glft tests
    -- local m = scene.entity("model")
    -- scene.replace(m, "Transform", { pos = { 0, 3, 0 } })
    -- graphics.gltf(m, "scenes/models/cube_test.glb")

    local ch = scene.entity("hashimoto")
    scene.replace(ch, "Transform", { pos = { -5, 0, 0 } })
-- entity-form gltf load: skinned model (the table API supports skinning too; kept on the entity path for full parity)
    graphics.gltf(ch, "local/basegen8.glb")
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

function _physics(self, dt)
    if released then
        return
    end
    pinTime = pinTime + dt
    if pinTime >= PIN_SECS then
        scene.set(clothH, "SoftBodyData.pinned", 0)
        released = true
    end
end

