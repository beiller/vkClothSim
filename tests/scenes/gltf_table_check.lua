-- gltf table check: graphics.gltf(path) -> table (no entities), scene.spawn materialization,
-- physics.convex_decompose on a raw Geometry table vs an entity.

local MAX_HULLS = 6
local MAX_VERTS = 12
local THRESHOLD = 0.05

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
    local ok4, hullsEnt = pcall(physics.convex_decompose, e, MAX_HULLS, MAX_VERTS, THRESHOLD)
    t.check("decompose(entity)", ok4 and #hullsEnt > 0, tostring(hullsEnt))
    if ok4 then
        local same = #hullsEnt == #hullsTbl
        for i = 1, #hullsEnt do
            if #hullsEnt[i] ~= #hullsTbl[i] then
                same = false
            end
        end
        t.check("entity hulls match table", same, string.format("ent=%d tbl=%d", #hullsEnt, #hullsTbl))
    end

    -- 5. error case: missing file must raise
    local okE = pcall(graphics.gltf, "does_not_exist.glb")
    t.check("missing file raises", not okE)
    t.done()
end

