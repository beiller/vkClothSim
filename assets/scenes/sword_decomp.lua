-- sword_decomp.lua
-- Load a glb once, decompose its simplified collision mesh into convex hulls (CoACD),
-- then spawn N rigid-body swords that all share one GPU mesh and one set of Jolt shapes.
-- The prototype tree stays alive (hidden) so the shared Geometry/shape entities are never
-- destroyed and their cached GPU mesh / Jolt shapes stay valid.
local M = {}

local GLB_PATH  = "local/sword_collider.glb" -- place the glb in assets/local/
local DRAWABLE  = "Mesh_0"      -- mesh name in the glb (rendered)
local COLLIDER  = "Mesh_0.001"  -- mesh name in the glb (hull source, hidden; NOTE: currently identical to Mesh_0)
local COUNT     = 30
local START_Y   = 5.0             -- drop height (m)
local MASS      = 0.0             -- kg; 0 = auto (shape volume x density)
local MAX_HULLS = 6
local MAX_VERTS = 12
local THRESHOLD = 0.05

local sharedGeom    -- drawable Geometry entity, referenced by every spawned renderable
local sharedMat     -- material of the drawable mesh
local sharedMeshPos -- body-local transform of the drawable mesh
local sharedMeshQuat
local sharedColls = {} -- array of { shape = ConvexHullShape entity, pos = vec3, quat = quat } (body-local)

local function makeGeom(t)
    local g = scene.entity()
    scene.replace(g, "Geometry", t)
    return g
end

-- spawn one rigid-body sword at world position p, reusing sharedGeom + sharedColls
local function spawnSword(p)
    local body = scene.entity()
    scene.replace(body, "Transform", { pos = p })

    local m = scene.entity()
    scene.replace(m, "Transform", { pos = sharedMeshPos, quat = sharedMeshQuat })
    scene.replace(m, "Renderable", { geom = sharedGeom })
    scene.replace(m, "MaterialRef", { material = sharedMat })
    scene.add_child(body, m)

    local cols = {}
    for _, sc in ipairs(sharedColls) do
        local c = scene.entity()
        scene.replace(c, "Collider", { shape = sc.shape })
        scene.replace(c, "Transform", { pos = sc.pos, quat = sc.quat })
        scene.add_child(body, c)
        table.insert(cols, c)
    end
    scene.replace(body, "RigidBody", { colliders = cols, mass = MASS })
end

function M._on_ready(self)
    -- ground: one entity with a visual box + static box collider; the soft sim's floor is explicit
    local ground = scene.entity("ground")
    scene.replace(ground, "Transform", { pos = { 0, -1, 0 } })
    scene.replace(ground, "Renderable", { geom = makeGeom(mesh.box({ 55, 1, 55 })) })
    scene.replace(ground, "MaterialRef", { material = scene.get_entity("mat_ground") })
    local gShape = scene.entity()
    scene.replace(gShape, "BoxShape", { half = { 40, 1, 40 } })
    local gCol = scene.entity()
    scene.replace(gCol, "Collider", { shape = gShape })
    scene.replace(ground, "KinematicBody", { colliders = { gCol } })

    -- load the glb once into a prototype root at the drop height
    local proto = scene.entity("sword_proto")
    scene.replace(proto, "Transform", { pos = { 0, START_Y, 0 } })
    graphics.gltf(proto, GLB_PATH)

    local dEnt = scene.get_entity(DRAWABLE)
    local cEnt = scene.get_entity(COLLIDER)
    if not dEnt or not cEnt then
        local n = #scene.find_components("Renderable", proto)
        print(string.format("SWORD FAIL: no entity named '%s' or '%s' (found %d renderables; set DRAWABLE/COLLIDER to the mesh names in the glb)", DRAWABLE, COLLIDER, n))
        return
    end

    -- hide both prototype meshes (merge replace keeps geom + material); spawned swords carry their own renderables
    scene.replace(cEnt, "Renderable", { visible = 0 })
    scene.replace(dEnt, "Renderable", { visible = 0 })

    sharedGeom = scene.component(dEnt, "Renderable").geom
    sharedMat = scene.component(dEnt, "MaterialRef").material

    -- body-local transform of the drawable mesh (proto has identity rotation)
    local wp = scene.world_pose(dEnt)
    sharedMeshPos = { wp.pos[1], wp.pos[2] - START_Y, wp.pos[3] }
    sharedMeshQuat = wp.quat

    -- decompose only the simplified collision mesh
    local hulls = physics.convex_decompose(scene.component(cEnt, "Renderable").geom, MAX_HULLS, MAX_VERTS, THRESHOLD)
    print(string.format("SWORD %d hulls from '%s'", #hulls, COLLIDER))
    for i, h in ipairs(hulls) do
        print(string.format("SWORD hull %d: %d verts", i, #h / 3))
    end
    if #hulls == 0 then
        print("SWORD FAIL: no hulls produced (degenerate mesh? see stderr for the CoACD error)")
        return
    end
    local cw = scene.world_pose(cEnt)
    for _, h in ipairs(hulls) do
        local s = scene.entity()
        scene.replace(s, "ConvexHullShape", { points = h })
        table.insert(sharedColls, { shape = s, pos = { cw.pos[1], cw.pos[2] - START_Y, cw.pos[3] }, quat = cw.quat })
    end

    -- spawn a grid of swords
    local perRow = math.ceil(COUNT / 2)
    for i = 1, COUNT do
        local row = (i - 1) % perRow
        local col = math.floor((i - 1) / perRow)
        spawnSword({ (row - (perRow - 1) / 2) * 0.8, START_Y, (col - 0.5) * 0.8 })
    end
    print(string.format("SWORD %d swords sharing 1 mesh + %d hull shapes", COUNT, #sharedColls))
end

function M._physics(self, dt)

end

return M
