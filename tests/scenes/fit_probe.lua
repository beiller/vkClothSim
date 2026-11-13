-- Probe for physics.fit_capsule: tapered frustum, blob->sphere (normal-aware), normals path, error cases.

local function frustumCloud(withNormals)
    local pos, nrm = {}, {}
    for a = 0, 39 do
        local s = 2.0 * a / 39
        local r = 0.5 - 0.2 * (s / 2.0)
        for b = 0, 23 do
            local th = 6.2831853 * b / 24
            local c, sn = math.cos(th), math.sin(th)
            table.insert(pos, r * c)
            table.insert(pos, r * sn)
            table.insert(pos, s)
            if withNormals then
                table.insert(nrm, c)
                table.insert(nrm, sn)
                table.insert(nrm, 0)
            end
        end
    end
    return pos, nrm
end

local function cylinderCloud()
    local pos, nrm = {}, {}
    for a = 0, 43 do
        local s = 2.2 * a / 43 - 1.1
        for b = 0, 23 do
            local th = 6.2831853 * b / 24
            local c, sn = math.cos(th), math.sin(th)
            table.insert(pos, c)
            table.insert(pos, sn)
            table.insert(pos, s)
            table.insert(nrm, c)
            table.insert(nrm, sn)
            table.insert(nrm, 0)
        end
    end
    return pos, nrm
end

function _on_ready()
    local pos, nrm = frustumCloud(true)
    local f, err = physics.fit_capsule(pos)
    t.check("frustum fits", f ~= nil, err)
    if not f then
        t.done()
        return
    end
    t.eq("frustum part count", #f.parts, 1)
    local p = f.parts[1]
    t.check("frustum axis z", math.abs(p.axis[3]) > 0.999, tostring(p.axis[3]))
    t.eq("frustum length", p.length, 2.0, 0.15)
    t.eq("frustum segments", p.segments, 1)
    local lo = math.min(p.tapers[1][3], p.tapers[1][4])
    local hi = math.max(p.tapers[1][3], p.tapers[1][4])
    t.eq("frustum r_lo", lo, 0.3, 0.05)
    t.eq("frustum r_hi", hi, 0.5, 0.05)

    local fn = physics.fit_capsule(pos, nrm)
    t.check("frustum + normals fits", fn ~= nil)
    if fn then
        t.eq("normals length", fn.parts[1].length, 2.0, 0.15)
    end

    local cpos, cnrm = cylinderCloud()
    local sf = physics.fit_capsule(cpos, cnrm)
    t.check("blob fits", sf ~= nil)
    if sf then
        local sp = sf.parts[1]
        t.eq("blob is sphere (length 0)", sp.length, 0.0, 1e-3)
        t.eq("blob r0", sp.tapers[1][3], 1.0, 0.05)
        t.eq("blob r1", sp.tapers[1][4], 1.0, 0.05)
        t.eq("blob cx", sp.origin[1], 0.0, 0.05)
        t.eq("blob cy", sp.origin[2], 0.0, 0.05)
        t.eq("blob cz", sp.origin[3], 0.0, 0.05)
    end

    local deg, derr = physics.fit_capsule({ 0, 0, 0, 1, 0, 0, 2, 0, 0 })
    t.check("degenerate -> nil + err", deg == nil and type(derr) == "string", tostring(deg))

    t.check("1-vertex raises", not pcall(physics.fit_capsule, { 1, 2, 3 }))
    t.check("nrm mismatch raises", not pcall(physics.fit_capsule, pos, { 0, 0, 1 }))
    t.done()
end
