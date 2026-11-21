local uib = require "ui"

local function geom(t)
    local g = scene.entity()
    scene.replace(g, "Geometry", t)
    return g
end

function create_cloth()
    local clothSpan = 2.0
    local clothDim = 96
    local clothGeom = geom(mesh.plane({ cw = clothDim, ch = clothDim, span = clothSpan }))
    clothH = scene.entity("cloth")
    scene.replace(clothH, "Transform", { pos = {0, 0, 0} })
    scene.replace(clothH, "Renderable", { geom = clothGeom })
    scene.replace(clothH, "MaterialRef", { material = scene.get_entity("mat_cloth") })
    scene.replace(clothH, "SoftBodyData",
                  { cons = mesh.planeCons({ cw = clothDim, ch = clothDim, span = clothSpan }), mass = 0.15 })

    local clothGrabRadius = 0.2
    local shape = scene.entity()
    scene.replace(shape, "SphereShape", { radius = clothGrabRadius })
    local col = scene.entity()
    scene.replace(col, "Collider", { shape = shape })

    -- pin the front edge (plane row gy=0 sits at z=-span/2) where it spawns; set_pins captures current positions
    local pinW = {}
    for i = 1, clothDim * clothDim do
        pinW[i] = 0.0
    end
    for i = 1, clothDim do -- pin a single row of the cloth
        pinW[i] = 1.0
    end

    local sphereGrabGeometry = scene.entity()
    scene.replace(sphereGrabGeometry, "Geometry", mesh.sphere({ radius = clothGrabRadius }))

    local sphereGrabMaterial = scene.entity()
    scene.replace(sphereGrabMaterial, "Material", {baseColor = {0,0,0}, emissive={2, 0.2, 0.2}})
    
    local sphereGrabMesh = scene.entity("cloth_grabber")
    scene.replace(sphereGrabMesh, "Transform", {pos = {0,3,0}})
    scene.replace(sphereGrabMesh, "Renderable", {geom = sphereGrabGeometry})
    scene.replace(sphereGrabMesh, "MaterialRef", {material = sphereGrabMaterial})
    scene.replace(sphereGrabMesh, "KinematicBody", { colliders = { col } })
    scene.replace(clothH, "Parent", {e = sphereGrabMesh})

    soft.set_pins(clothH, pinW)
end

function _on_ready(self)
    local ground = scene.entity("ground")
    scene.replace(ground, "Transform", { pos = { 0, -1, 0 } })
    scene.replace(ground, "Renderable", { geom = geom(mesh.box({ 55, 1, 55 })) })
    scene.replace(ground, "MaterialRef", { material = scene.get_entity("mat_ground") })
    -- static ground collider (kinematic = immovable)
    local gShape = scene.entity()
    scene.replace(gShape, "BoxShape", { half = { 55, 1, 55 } })
    local gCol = scene.entity()
    scene.replace(gCol, "Collider", { shape = gShape })
    scene.replace(ground, "KinematicBody", { colliders = { gCol } })

    self.metricsText = uib.text(uib.make_window("metrics", 160, 180, { 0.45, 1.72, 2.8 }), "")

    soft.set_floor(0.001)
    create_cloth()
end


-- refresh the metrics window from the engine's performance string every frame
function _animate(self, dt)
    local t = scene.component(self.metricsText, "UIText")
    if t then
        t.label = engine.performance_metrics()
    end
end


function _physics(self, dt)

end
