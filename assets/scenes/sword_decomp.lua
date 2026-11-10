-- sword_decomp.lua
-- Parse a glb once as a table, decompose its collision mesh into convex hulls (CoACD),
-- then spawn N rigid-body swords that all share one GPU mesh and one set of Jolt shapes.
-- The parsed table is never materialized itself: the shared mesh/material/shape entities
-- are spawned explicitly and referenced by handle from every copy.

local GLB_PATH  = "sword_collider.glb"
local DRAWABLE  = "Mesh_0"      -- mesh name in the glb (rendered)
local COLLIDER  = "Mesh_0.001"  -- mesh name in the glb (hull source, not rendered)
local COUNT     = 100
local START_Y   = 5.0             -- drop height (m)
local MASS      = 0.5             -- kg; 0 = auto (shape volume x density)
local MAX_HULLS = 6
local MAX_VERTS = 12
local THRESHOLD = 0.05

local function makeGeom(t)
    local g = scene.entity()
    scene.replace(g, "Geometry", t)
    return g
end

-- random unit quaternion: uniform random axis on the sphere + uniform random angle
local function randQuat()
    local z = 2 * math.random() - 1
    local a = 2 * math.pi * math.random()
    local r = math.sqrt(math.max(0, 1 - z * z))
    return math.qaxis({ math.cos(a) * r, math.sin(a) * r, z }, 2 * math.pi * math.random())
end

function _on_ready(self)
    math.randomseed(20240501) -- fixed seed -> reproducible ball; delete for a different scatter each run
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

    -- parse the glb once (no world entities); nothing in the world depends on `sword`
    local sword = graphics.gltf(GLB_PATH)
    if not sword[DRAWABLE] or not sword[COLLIDER] then
        print(string.format("SWORD FAIL: no node named '%s' or '%s' in the glb", DRAWABLE, COLLIDER))
        return
    end

    -- 1. shared graphics: the drawable mesh as a pure Geometry resource (no Renderable, so it
    -- never draws on its own) + its material + textures, spawned once; swords reference by handle
    local matKey = sword[DRAWABLE].MaterialRef and sword[DRAWABLE].MaterialRef.material
    local sharedTbl = { [DRAWABLE] = { Geometry = sword[DRAWABLE].Geometry } }
    if matKey then
        sharedTbl[matKey] = sword[matKey]
    end
    -- copy the textures ensure they are uploaded to GPU
    for name, node in pairs(sword) do
        if node.Texture then
            sharedTbl[name] = node
        end
    end
    local shared = scene.spawn(sharedTbl)

    -- 2. decompose the raw collider table (no entity involved)
    local hulls = physics.convex_decompose(sword[COLLIDER].Geometry, MAX_HULLS, MAX_VERTS, THRESHOLD)
    print(string.format("SWORD %d hulls from '%s'", #hulls, COLLIDER))
    for i, h in ipairs(hulls) do
        print(string.format("SWORD hull %d: %d verts", i, #h / 3))
    end
    if #hulls == 0 then
        print("SWORD FAIL: no hulls produced (degenerate mesh? see stderr for the CoACD error)")
        return
    end

    -- 3. shared collision shapes: one batched spawn -> name->entity map
    local hullTbl = {}
    for i, pts in ipairs(hulls) do
        hullTbl["hull_" .. i] = { ConvexHullShape = { points = pts } }
    end
    local sharedHulls = scene.spawn(hullTbl)

    -- 4. copies: one node per sword (body + renderable), collider nodes as siblings;
    -- cross-copy references use the integer handles from the returned maps. Positions are a
    -- fibonacci sphere -> the swords bundle into a ball around (0, START_Y, 0)
    local R = 1.8  -- ball radius (m)
    local golden = math.pi * (3 - math.sqrt(5))
    for i = 1, COUNT do
        local t = (i - 0.5) / COUNT
        local y = 1 - 2 * t
        local rr = math.sqrt(math.max(0, 1 - y * y))
        local phi = (i - 1) * golden
        local colNames = {}
        for j in ipairs(hulls) do
            colNames[j] = "col_" .. j
        end
        local copy = {
            ["sword" .. i] = {
                Transform   = { pos = { math.cos(phi) * rr * R, START_Y + y * R, math.sin(phi) * rr * R },
                               quat = randQuat() },
                Renderable  = { geom = shared[DRAWABLE] },
                RigidBody   = { mass = MASS, colliders = colNames },
            },
        }
        if matKey then
            copy["sword" .. i].MaterialRef = { material = shared[matKey] }
        end
        for j in ipairs(hulls) do
            copy["col_" .. j] = { Collider = { shape = sharedHulls["hull_" .. j] } }
        end
        scene.spawn(copy)
    end
    print(string.format("SWORD %d swords sharing 1 mesh + %d hull shapes", COUNT, #hulls))
    print("SWORDDBG bodies=" .. #scene.find_components("RigidBody") .. " renderables=" .. #scene.find_components("Renderable"))
end

