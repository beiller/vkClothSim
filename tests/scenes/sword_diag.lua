
function _on_ready(self)
    local root = scene.entity("sword_root")
    scene.replace(root, "Transform", { pos = { 0, 12, 0 } })
    scene.spawn(graphics.gltf("sword.glb"), root)

    print(string.format("DIAG Geometry(all)=%d", #scene.find_components("Geometry")))
    print(string.format("DIAG Renderable(all)=%d", #scene.find_components("Renderable")))
    t.check("geometry spawned", #scene.find_components("Geometry") > 0)
    t.check("renderable spawned", #scene.find_components("Renderable", root) > 0)

    local gAll = scene.find_components("Geometry")
    if #gAll > 0 then
        local ok, wp = pcall(scene.world_pose, gAll[1])
        t.check("world_pose(geom)", ok and wp ~= nil)
    end

    local rRoot = scene.find_components("Renderable", root)
    if #rRoot > 0 then
        local ok, wp = pcall(scene.world_pose, rRoot[1])
        t.check("world_pose(renderable)", ok and wp ~= nil)
        -- does scene.component expose the referenced geometry entity?
        local okc, comp = pcall(scene.component, rRoot[1], "Renderable")
        t.check("component(Renderable) has geom", okc and type(comp) == "table" and comp.geom ~= nil)
        if okc and type(comp) == "table" then
            local okw, wp = pcall(scene.world_pose, comp.geom)
            t.check("world_pose(geom via component)", okw and wp ~= nil)
        end
    end
    t.done()
end

