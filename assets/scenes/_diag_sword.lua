local M = {}

function M._on_ready(self)
    local root = scene.entity("sword_root")
    scene.replace(root, "Transform", { pos = { 0, 12, 0 } })
    scene.spawn(graphics.gltf("local/sword.glb"), root)

    print(string.format("DIAG Geometry(all)=%d", #scene.find_components("Geometry")))
    print(string.format("DIAG Renderable(all)=%d", #scene.find_components("Renderable")))
    print(string.format("DIAG Renderable(root)=%d", #scene.find_components("Renderable", root)))

    local gAll = scene.find_components("Geometry")
    if #gAll > 0 then
        local g = gAll[1]
        local ok, wp = pcall(scene.world_pose, g)
        print(string.format("DIAG geom[1] world_pose ok=%s", tostring(ok)))
        if ok then
            print(string.format("DIAG geom[1] pos=(%.2f,%.2f,%.2f)", wp.pos[1], wp.pos[2], wp.pos[3]))
        end
    end

    local rRoot = scene.find_components("Renderable", root)
    if #rRoot > 0 then
        local r = rRoot[1]
        local ok, wp = pcall(scene.world_pose, r)
        print(string.format("DIAG renderable[1] world_pose ok=%s", tostring(ok)))
        if ok then
            print(string.format("DIAG renderable[1] pos=(%.2f,%.2f,%.2f)", wp.pos[1], wp.pos[2], wp.pos[3]))
        end
        -- does scene.component expose the referenced geometry entity?
        local okc, comp = pcall(scene.component, r, "Renderable")
        print(string.format("DIAG component(Renderable) ok=%s", tostring(okc)))
        if okc and type(comp) == "table" then
            for k, v in pairs(comp) do
                print(string.format("DIAG   field %s = %s", tostring(k), tostring(v)))
            end
            local g = comp.geom
            print(string.format("DIAG geom id=%s (type=%s)", tostring(g), type(g)))
            local okw, wp = pcall(scene.world_pose, g)
            print(string.format("DIAG world_pose(geom) ok=%s", tostring(okw)))
            if okw then
                print(string.format("DIAG geom pos=(%.2f,%.2f,%.2f)", wp.pos[1], wp.pos[2], wp.pos[3]))
            end
            local okd, hulls = pcall(physics.convex_decompose, g)
            print(string.format("DIAG convex_decompose(geom) ok=%s", tostring(okd)))
            if okd and type(hulls) == "table" then
                print(string.format("DIAG hulls=%d, hull1 verts=%d", #hulls, (hulls[1] and #hulls[1]) or 0))
            end
        end
    end
end

return M
