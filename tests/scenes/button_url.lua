-- target_url dispatch: ui.fire_button calls an exported function via "name.LuaScript.fn"
-- (arg 1 = the button entity); UIWidget/UIButton are part of the component API.

local hits = {}
notfn = 42   -- exported non-function, for the error path

function hit(b)
    table.insert(hits, b)
end

function _on_ready(self)
    -- UIWidget/UIButton through the component API
    local b = scene.entity()
    scene.emplace(b, "UIWidget", { order = 3 })
    scene.emplace(b, "UIButton", { label = "go", target_url = "probe.LuaScript.hit" })
    t.eq("order read back", scene.component(b, "UIWidget").order, 3)
    t.eq("target_url read back", scene.component(b, "UIButton").target_url, "probe.LuaScript.hit")

    -- dispatch through the button's own target_url; arg 1 is an entity (null via fire_button)
    ui.fire_button(scene.component(b, "UIButton").target_url)
    t.eq("fired once", #hits, 1)
    t.check("arg is an entity", type(hits[1]) == "number")

    -- error paths: unknown function / non-function export / unknown entity (logged, not fatal)
    ui.fire_button("probe.LuaScript.nope")
    ui.fire_button("probe.LuaScript.notfn")
    ui.fire_button("ghost.LuaScript.hit")
    t.eq("still one hit", #hits, 1)

    t.done()
end
