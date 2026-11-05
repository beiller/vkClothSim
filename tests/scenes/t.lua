-- test verdict module; loaded as global `t` by the engine (see SceneScript::loadTestStdlib)
local EPS = 1e-6 -- default tolerance for number comparisons (float round-trip noise)
local T = { npass = 0, nfail = 0 }

local function numEq(g, w, tol)
    return type(g) == "number" and math.abs(g - w) <= (tol or EPS)
end

function T.check(name, cond, detail)
    if cond then
        T.npass = T.npass + 1
        print("PASS " .. name)
    else
        T.nfail = T.nfail + 1
        print("FAIL " .. name .. (detail and (" (" .. tostring(detail) .. ")") or ""))
    end
end

function T.eq(name, got, want, tol)
    if type(want) == "table" then
        local ok = type(got) == "table" and #got == #want
        if ok then
            for i = 1, #want do
                local g, w = got[i], want[i]
                if type(w) == "number" then
                    if not numEq(g, w, tol) then ok = false break end
                elseif g ~= w then
                    ok = false
                    break
                end
            end
        end
        T.check(name, ok, string.format("got [%s] want [%s]", table.concat(got and got or {}, ","),
                                      table.concat(want, ",")))
    else
        local ok
        if type(want) == "number" then
            ok = numEq(got, want, tol)
        else
            ok = got == want
        end
        T.check(name, ok, string.format("got %s want %s", tostring(got), tostring(want)))
    end
end

function T.done()
    print(string.format("TTEST pass=%d fail=%d", T.npass, T.nfail))
end

return T
