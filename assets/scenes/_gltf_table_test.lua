-- _gltf_table_test.lua
-- P2 verification: graphics.gltf(path) -> table (no entities), scene.spawn materialization,
-- physics.convex_decompose on a raw Geometry table vs an entity.
local M = {}

local MAX_HULLS = 6
local MAX_VERTS = 12
local THRESHOLD = 0.05

local function fail(msg)
    print("P2FAIL " .. msg)
end

function M._on_ready(self)
    -- 1. parse as a table; zero world entities should be created by this call
    local ok, tbl = pcall(graphics.gltf, "local/sword_collider.glb")
    if not ok then
        fail("gltf: " .. tostring(tbl))
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
    if not col or not col.Geometry then
        fail("no Mesh_0.001 Geometry in table")
        return
    end
    local ok2, hullsTbl = pcall(physics.convex_decompose, col.Geometry, MAX_HULLS, MAX_VERTS, THRESHOLD)
    if not ok2 then
        fail("decompose(table): " .. tostring(hullsTbl))
        return
    end
    print(string.format("P2HULLS_T n=%d first=%d", #hullsTbl, #hullsTbl[1] // 3))

    -- 3. spawn the whole table (meshes + material + textures)
    local ok3, spawned = pcall(scene.spawn, tbl)
    if not ok3 then
        fail("spawn: " .. tostring(spawned))
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
    if not rend or rend.geom ~= e then
        fail("spawn: Renderable.geom not self (got " .. tostring(rend and rend.geom) .. ")")
    else
        print("P2GEOM_OK")
    end

    -- 4. convex_decompose on the spawned entity; must match the table result
    local ok4, hullsEnt = pcall(physics.convex_decompose, e, MAX_HULLS, MAX_VERTS, THRESHOLD)
    if not ok4 then
        fail("decompose(entity): " .. tostring(hullsEnt))
        return
    end
    local same = #hullsEnt == #hullsTbl
    if same then
        for i = 1, #hullsEnt do
            if #hullsEnt[i] ~= #hullsTbl[i] then
                same = false
            end
        end
    end
    print(same and "P2HULLS_SAME" or string.format("P2HULLS_DIFF ent=%d tbl=%d", #hullsEnt, #hullsTbl))

    -- 5. error case: missing file must raise
    local okE = pcall(graphics.gltf, "local/does_not_exist.glb")
    print(okE and "P2ERR_UNEXPECTED" or "P2ERR_OK")
end

return M
