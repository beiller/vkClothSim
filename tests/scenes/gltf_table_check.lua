-- gltf table check: graphics.gltf(path) -> table (no entities), scene.spawn materialization,
-- physics.convex_decompose on a raw Geometry table vs an entity, and a RigidBody built from the
-- decomposed hulls settling to rest (folded in from decomp_check).

local MAX_HULLS = 6
local MAX_VERTS = 12
local THRESHOLD = 0.05

local tick = 0

function _on_ready(self)
    -- 1. parse as a table; zero world entities should be created by this call
    local ok, tbl = pcall(graphics.gltf, "sword_collider.glb")
    t.check("gltf table parse", ok, tostring(tbl))
    if not ok then
        t.done()
        return
    end

    local keys = {}
    for k in pairs(tbl) do
        table.insert(keys, k)
    end
    table.sort(keys)
    print("P2KEYS " .. table.concat(keys, ","))

    local nMesh, nMat, nTex = 0, 0, 0
    for k, node in pairs(tbl) do
        if node.Geometry then
            nMesh = nMesh + 1
            print(string.format("P2MESH %s verts=%d idx=%d matRef=%s", k, #node.Geometry.pos // 3,
                              node.Geometry.idx and #node.Geometry.idx or 0,
                              node.MaterialRef and node.MaterialRef.material or "-"))
        end
        if node.Material then
            nMat = nMat + 1
            local m = node.Material
            print(string.format("P2MAT %s bc=(%.3f,%.3f,%.3f) met=%.2f rgh=%.2f tex=%s texMR=%s", k,
                              m.baseColor[1], m.baseColor[2], m.baseColor[3], m.metallic or 0,
                              m.roughness or 0, tostring(m.tex), tostring(m.texMR)))
        end
        if node.Texture then
            nTex = nTex + 1
            print(string.format("P2TEX %s path=%s space=%s", k, node.Texture.path, node.Texture.space))
        end
    end
    print(string.format("P2COUNTS mesh=%d mat=%d tex=%d", nMesh, nMat, nTex))

    -- 2. convex_decompose on the raw collider table (no entity involved)
    local col = tbl["Mesh_0.001"]
    t.check("collider node in table", col and col.Geometry ~= nil)
    if not col or not col.Geometry then
        t.done()
        return
    end
    local ok2, hullsTbl = pcall(physics.convex_decompose, col.Geometry, MAX_HULLS, MAX_VERTS, THRESHOLD)
    t.check("decompose(table)", ok2 and #hullsTbl > 0, tostring(hullsTbl))
    if not ok2 then
        t.done()
        return
    end
    print(string.format("P2HULLS_T n=%d first=%d", #hullsTbl, #hullsTbl[1] // 3))

    -- 3. spawn the whole table (meshes + material + textures)
    local ok3, spawned = pcall(scene.spawn, tbl)
    t.check("spawn", ok3 and spawned ~= nil, tostring(spawned))
    if not ok3 then
        t.done()
        return
    end
    local nSpawned = 0
    for _ in pairs(spawned) do
        nSpawned = nSpawned + 1
    end
    print(string.format("P2SPAWN n=%d", nSpawned))

    -- Renderable.geom must be wired to the node's own entity
    local e = spawned["Mesh_0.001"]
    local rend = scene.component(e, "Renderable")
    t.check("spawn: Renderable.geom is self", rend and rend.geom == e,
            tostring(rend and rend.geom))

    -- 4. convex_decompose on the spawned entity; must match the table result
    local ok4, hulls = pcall(physics.convex_decompose, e, MAX_HULLS, MAX_VERTS, THRESHOLD)
    t.check("decompose(entity)", ok4 and #hulls > 0, tostring(hulls))
    if not ok4 or #hulls == 0 then
        t.done()
        return
    end
    local same = #hulls == #hullsTbl
    for i = 1, #hulls do
        if #hulls[i] ~= #hullsTbl[i] then
            same = false
        end
    end
    t.check("entity hulls match table", same, string.format("ent=%d tbl=%d", #hulls, #hullsTbl))

    -- 5. error case: missing file must raise
    local okE = pcall(graphics.gltf, "does_not_exist.glb")
    t.check("missing file raises", not okE)

    -- 6. hulls -> ConvexHullShape colliders -> RigidBody: dropped from y=30, must settle on the ground
    local ground = scene.entity("ground")
    scene.replace(ground, "Transform", { pos = { 0, -1, 0 } })
    local gShape = scene.entity()
    scene.replace(gShape, "BoxShape", { half = { 55, 1, 55 } })
    local gCol = scene.entity()
    scene.replace(gCol, "Collider", { shape = gShape })
    scene.replace(ground, "KinematicBody", { colliders = { gCol } })

    local body = scene.entity("decompBody")
    scene.replace(body, "Transform", { pos = { 0, 30, 0 } })
    local cols = {}
    for _, h in ipairs(hulls) do
        local s = scene.entity()
        scene.replace(s, "ConvexHullShape", { points = h })
        local c = scene.entity()
        scene.replace(c, "Collider", { shape = s })
        table.insert(cols, c)
    end
    scene.replace(body, "RigidBody", { colliders = cols })
end

local restY = nil
function _physics(self, dt)
    tick = tick + 1
    if tick % 30 == 0 then
        local y = scene.get_pose("decompBody").pos[2]
        print(string.format("DECOMP t=%d bodyY=%.3f", tick, y))
        if tick == 870 then
            restY = y -- reference sample; the check at 900 compares against this (30 ticks earlier)
        end
    end
    if tick == 900 then -- free fall from y=30 lands ~tick 594; by 870 the body is at rest
        local y = scene.get_pose("decompBody").pos[2]
        t.check("hull body settled (stable, above ground)", restY and y > 0 and math.abs(y - restY) < 0.05,
                string.format("y=%.3f restY=%s", y, tostring(restY)))
        t.done()
    end
end

