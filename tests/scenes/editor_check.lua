-- the pure-lua scene editor: one UISection per named entity, one widget per reflected field,
-- each bound to "name.Component.field" (read-write value widgets, read-only text for refs/arrays)

local uib = require("ui")

local function inList(list, v)
    for _, x in ipairs(list) do
        if x == v then return true end
    end
    return false
end

local function namedHas(name)
    for _, p in ipairs(scene.named()) do
        if p.name == name then return true end
    end
    return false
end

-- the first widget of `comp` whose Bind url is `url` (nil when none)
local function findWidget(comp, url)
    for _, e in ipairs(scene.find_components(comp)) do
        local b = scene.component(e, "Bind")
        if b and b.url == url then
            return e
        end
    end
    return nil
end

local function hasSection(label)
    for _, e in ipairs(scene.find_components("UISection")) do
        if scene.component(e, "UISection").label == label then
            return true
        end
    end
    return false
end

function _on_ready(self)
    local rb = scene.get_entity("rb")
    local cam = scene.get_entity("cam")

    -- the new introspection bindings the editor is built on
    t.check("named() has rb", namedHas("rb"))
    t.check("named() has cam", namedHas("cam"))
    t.check("components(rb) has RigidBody", inList(scene.components(rb), "RigidBody"))
    t.check("components(cam) has Camera", inList(scene.components(cam), "Camera"))

    -- build the editor into a tab (headless: no main() call, so this is the only invocation)
    local win = uib.make_window("editor test", 320, 200, { 0, 1.5, 2.8 })
    uib.editor(uib.tab(win, "editor"))

    -- one section per named entity that has components
    t.check("section rb", hasSection("rb"))
    t.check("section cam", hasSection("cam"))

    -- read-write value widgets carry a Bind with dir Both (0)
    local mass = findWidget("UISliderF", "rb.RigidBody.mass")
    t.check("mass slider exists", mass ~= nil)
    if mass then t.eq("mass slider dir Both", scene.component(mass, "Bind").dir, 0) end
    local fov = findWidget("UISliderF", "cam.Camera.fovDeg")
    t.check("fov slider exists", fov ~= nil)
    local act = findWidget("UICheckbox", "cam.Camera.active")
    t.check("active checkbox exists", act ~= nil)
    if act then t.eq("active dir Both", scene.component(act, "Bind").dir, 0) end

    -- array/ref fields are read-only text with a Bind dir Read (1)
    local col = findWidget("UIText", "rb.RigidBody.colliders")
    t.check("colliders read-only text exists", col ~= nil)
    if col then t.eq("colliders dir Read", scene.component(col, "Bind").dir, 1) end

    t.done()
end
