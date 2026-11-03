-- sword_decomp.lua
-- Load a glb, decompose its meshes into convex hulls (CoACD), build a RigidBody
-- from those hulls, parent the rendered mesh to the body, and watch it fall.
--
-- Note: for non-skinned glb meshes the loader stores vertex data on an orphan
-- Geometry entity (no Name/Parent), so we reach it via the Renderable's `geom`
-- reference (scene.component(r, "Renderable").geom) and take the world pose from
-- the renderable itself (which carries the transform chain under root).
local M = {}

local GLB_PATH  = "local/sword_collider.glb" -- place the glb in assets/local/
local START_Y   = 12.0             -- drop height (m)
local MASS      = 0.0              -- kg; 0 = auto (shape volume x density)
local MAX_HULLS = 32
local MAX_VERTS = 64
local THRESHOLD = 0.05

local tick = 0
local lastY = nil
local fell = false

local function makeGeom(t)
    local g = scene.entity()
    scene.replace(g, "Geometry", t)
    return g
end

function M._on_ready(self)
    -- ground: static box, top surface at y=0
    local ground = scene.entity("ground")
    scene.replace(ground, "Transform", { pos = { 0, -1, 0 } })
    local gShape = scene.entity()
    scene.replace(gShape, "BoxShape", { half = { 50, 1, 50 } })
    local gCol = scene.entity()
    scene.replace(gCol, "Collider", { shape = gShape })
    scene.replace(ground, "KinematicBody", { colliders = { gCol } })
    -- visible ground mesh + material so the landing is visible (matches the collider half-extents)
    scene.replace(ground, "Renderable", { geom = makeGeom(mesh.box({ 50, 1, 50 })) })
    scene.replace(ground, "MaterialRef", { material = scene.get_entity("mat_ground") })

    -- load the glb into a root entity at the drop height
    local root = scene.entity("sword_root")
    scene.replace(root, "Transform", { pos = { 0, START_Y, 0 } })
    graphics.gltf(root, GLB_PATH)

    -- physics body starts co-located with the model (identity rotation)
    local body = scene.entity("sword_body")
    scene.replace(body, "Transform", { pos = { 0, START_Y, 0 } })

    -- parent the whole loaded model to the body so it follows as it falls
    scene.add_child(body, root, { keepWorld = true })

    -- decompose every mesh; attach a ConvexHullShape collider per hull
    local renders = scene.find_components("Renderable", root)
    if #renders == 0 then
        print("SWORD FAIL: no Renderable under " .. GLB_PATH)
        return
    end
    local cols, total = {}, 0
    for i, r in ipairs(renders) do
        local comp = scene.component(r, "Renderable")
        local geom = comp and comp.geom
        if not geom then
            print(string.format("SWORD mesh %d: no geometry reference", i))
        else
            local wp = scene.world_pose(r) -- world {pos, quat} of this mesh (via renderable)
        local hulls = physics.convex_decompose(geom, MAX_HULLS, MAX_VERTS, THRESHOLD)
        print(string.format("SWORD mesh %d: %d hulls", i, #hulls))
        for _, h in ipairs(hulls) do
            total = total + 1
            local s = scene.entity()
            scene.replace(s, "ConvexHullShape", { points = h })
            local c = scene.entity()
            scene.replace(c, "Collider", { shape = s })
            -- body has identity rotation: collider local = mesh world pos - body pos
            scene.replace(c, "Transform", {
                pos  = { wp.pos[1], wp.pos[2] - START_Y, wp.pos[3] },
                quat = wp.quat,
            })
            scene.add_child(body, c)
            table.insert(cols, c)
        end
        end
    end
    if total == 0 then
        print("SWORD FAIL: no hulls produced")
        return
    end
    print(string.format("SWORD %d meshes -> %d hull colliders (mass=%s)", #renders, total, tostring(MASS)))
    scene.replace(body, "RigidBody", { colliders = cols, mass = MASS })
end

function M._physics(self, dt)

end

return M
