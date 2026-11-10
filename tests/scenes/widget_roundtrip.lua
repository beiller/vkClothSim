-- every UI widget component through the component API: emplace + read-back round-trips,
-- scene.fields metadata, a bound slider, and the pure-lua canvas window (ui.make_window)

local uib = require("ui")

function _on_ready(self)
    -- scene.fields metadata
    local f = scene.fields("UISliderF")
    t.check("fields is an array", #f >= 6)
    local found = false
    for _, x in ipairs(f) do
        if x.name == "change_url" and x.kind == "str" then
            found = true
        end
    end
    t.check("change_url field present", found)

    -- UIWidget + UIButton
    local b = scene.entity()
    scene.emplace(b, "UIWidget", { order = 7 })
    scene.emplace(b, "UIButton", { label = "go", target_url = "probe.LuaScript.hit" })
    t.eq("button label", scene.component(b, "UIButton").label, "go")

    -- UICanvas / GPUTexture / UIWindow (the canvas assembly, by hand)
    local canvas = scene.entity()
    scene.replace(canvas, "Transform", { pos = { 0, 1.5, 2.8 } })
    scene.emplace(canvas, "UICanvas", { w = 0.644, h = 0.264 })
    scene.emplace(canvas, "GPUTexture", { w = 293, h = 120 })
    t.eq("canvas w", scene.component(canvas, "UICanvas").w, 0.644)
    t.eq("tex h", scene.component(canvas, "GPUTexture").h, 120)
    local win = scene.entity()
    scene.emplace(win, "UIWindow", { title = "t", canvas = canvas, w = 293, h = 120, flags = 294, order = 1 })
    t.eq("win canvas ref", scene.component(win, "UIWindow").canvas, canvas)

    -- UITab / UIText / UISection
    local tab = scene.entity()
    scene.replace(tab, "Parent", { e = win })
    scene.emplace(tab, "UIWidget", { order = 2 })
    scene.emplace(tab, "UITab", { label = "scene" })
    t.eq("tab label", scene.component(tab, "UITab").label, "scene")
    local txt = scene.entity()
    scene.replace(txt, "Parent", { e = tab })
    scene.emplace(txt, "UIWidget", { order = 3 })
    scene.emplace(txt, "UIText", { label = "hello" })
    t.eq("text label", scene.component(txt, "UIText").label, "hello")
    local sec = scene.entity()
    scene.replace(sec, "Parent", { e = tab })
    scene.emplace(sec, "UIWidget", { order = 4 })
    scene.emplace(sec, "UISection", { label = "s", collapsible = false })
    t.check("section not collapsible", not scene.component(sec, "UISection").collapsible)

    -- value widgets + change_url round-trip
    local s = scene.entity()
    scene.replace(s, "Parent", { e = tab })
    scene.emplace(s, "UIWidget", { order = 5 })
    scene.emplace(s, "UISliderF", { label = "f", value = 1.5, lo = 0, hi = 2, fmt = "%.1f", change_url = "probe.LuaScript.hit" })
    t.eq("slider value", scene.component(s, "UISliderF").value, 1.5)
    t.eq("slider change_url", scene.component(s, "UISliderF").change_url, "probe.LuaScript.hit")
    local si = scene.entity()
    scene.replace(si, "Parent", { e = tab })
    scene.emplace(si, "UIWidget", { order = 6 })
    scene.emplace(si, "UISliderI", { label = "i", value = 3, lo = 1, hi = 9 })
    t.eq("islider value", scene.component(si, "UISliderI").value, 3)
    local cb = scene.entity()
    scene.replace(cb, "Parent", { e = tab })
    scene.emplace(cb, "UIWidget", { order = 8 })
    scene.emplace(cb, "UICheckbox", { label = "c", value = true, change_url = "probe.LuaScript.hit" })
    t.check("checkbox value", scene.component(cb, "UICheckbox").value)
    local ce = scene.entity()
    scene.replace(ce, "Parent", { e = tab })
    scene.emplace(ce, "UIWidget", { order = 9 })
    scene.emplace(ce, "UIColorEdit", { label = "col", value = { 1, 0.5, 0 } })
    t.eq("color g", scene.component(ce, "UIColorEdit").value[2], 0.5)
    local dv = scene.entity()
    scene.replace(dv, "Parent", { e = tab })
    scene.emplace(dv, "UIWidget", { order = 10 })
    scene.emplace(dv, "UIDragV3", { label = "d", value = { 1, 2, 3 }, speed = 0.5 })
    t.eq("drag z", scene.component(dv, "UIDragV3").value[3], 3)

    -- a bound slider: widget + Bind, initial value from the target field
    local bs = scene.entity()
    scene.replace(bs, "Parent", { e = tab })
    scene.emplace(bs, "UIWidget", { order = 11 })
    scene.emplace(bs, "UISliderF", { label = "mass", value = scene.get("rb.RigidBody.mass"), lo = 0, hi = 10 })
    scene.emplace(bs, "Bind", { url = "rb.RigidBody.mass" })
    t.eq("bound slider init", scene.component(bs, "UISliderF").value, 2.0)

    -- the pure-lua canvas window (mesh.quad + full assembly)
    local win2 = uib.make_window("test", 100, 50, { 0, 1, 2 })
    t.check("window has UIWindow", scene.has(win2, "UIWindow"))
    local c2 = scene.component(win2, "UIWindow").canvas
    t.check("canvas has UICanvas", scene.has(c2, "UICanvas"))
    t.check("canvas has GPUTexture", scene.has(c2, "GPUTexture"))
    t.check("canvas has Renderable", scene.has(c2, "Renderable"))
    t.eq("window pos x", scene.component(c2, "Transform").pos[1], 0)

    -- the metrics window: spawned manually by the script, updated from the engine's perf string
    local mt = uib.text(uib.make_window("metrics", 100, 50, { 0, 1, 3 }), "")
    t.check("metrics text has UIText", scene.has(mt, "UIText"))
    local m = engine.performance_metrics()
    t.check("perf metrics is a non-empty string", type(m) == "string" and #m > 0)
    t.check("perf metrics mentions fps", m:find("fps") ~= nil)
    scene.component(mt, "UIText").label = m
    t.eq("live view label write", scene.component(mt, "UIText").label, m)

    t.done()
end
