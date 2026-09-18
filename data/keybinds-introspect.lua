-- hypr-shell: keybinding introspection for the keybindings overlay.
--
-- Hyprland >= 0.56 configs are Lua, and `hyprctl binds` reports every Lua
-- bind as dispatcher "__lua" with a callback id — nothing says what a key
-- does. This script runs the user's config a second time, in a plain `lua`
-- process, with a recording stand-in for the `hl` API: every hl.bind() call
-- is written to stdout as one JSON object per line (keys string, dispatcher
-- path + arguments, options, the nearest short comment above the call as its
-- section). Anything else the config does with `hl` is swallowed by proxies.
--
-- Usage: lua - <path/to/hyprland.lua>   (the shell feeds this file on stdin)

local config_path = arg and arg[1]
if not config_path then
    io.stderr:write("usage: lua - <hyprland.lua>\n")
    os.exit(2)
end
local config_dir = config_path:match("^(.*)/[^/]*$") or "."

-- ---------------------------------------------------------------------------
-- sandbox: the config must not run anything while we replay it

local real_open = io.open
io.open = function(path, mode)
    if mode and mode:find("[wa+]") then return nil, "hypr-shell: sandboxed" end
    return real_open(path, mode)
end
io.popen = function() return nil, "hypr-shell: sandboxed" end
os.execute = function() return nil, "hypr-shell: sandboxed" end
os.remove = function() return nil, "hypr-shell: sandboxed" end
os.rename = function() return nil, "hypr-shell: sandboxed" end
os.tmpname = function() return "/dev/null" end
os.exit = function() end
io.write = function(...) return io.stderr:write(...) end
print = function(...)
    local parts = {}
    for i = 1, select("#", ...) do parts[#parts + 1] = tostring(select(i, ...)) end
    io.stderr:write(table.concat(parts, "\t"), "\n")
end

-- ---------------------------------------------------------------------------
-- JSON output (strings, numbers, booleans, arrays, string-keyed tables)

local function json_string(s)
    s = tostring(s)
    s = s:gsub('[%c"\\]', function(c)
        if c == '"' then return '\\"' end
        if c == "\\" then return "\\\\" end
        if c == "\n" then return "\\n" end
        if c == "\t" then return "\\t" end
        if c == "\r" then return "\\r" end
        return string.format("\\u%04x", c:byte())
    end)
    return '"' .. s .. '"'
end

local json_value
local function json_table(t)
    local n = #t
    local is_array = n > 0
    if is_array then
        for k in pairs(t) do
            if type(k) ~= "number" then is_array = false break end
        end
    end
    local parts = {}
    if is_array then
        for i = 1, n do parts[#parts + 1] = json_value(t[i]) end
        return "[" .. table.concat(parts, ",") .. "]"
    end
    local keys = {}
    for k in pairs(t) do keys[#keys + 1] = tostring(k) end
    table.sort(keys)
    for _, k in ipairs(keys) do
        local v = t[k]
        if v == nil then v = t[tonumber(k)] end
        parts[#parts + 1] = json_string(k) .. ":" .. json_value(v)
    end
    return "{" .. table.concat(parts, ",") .. "}"
end

function json_value(v)
    local tv = type(v)
    if tv == "string" then return json_string(v) end
    if tv == "number" then
        if v ~= v or v == math.huge or v == -math.huge then return "null" end
        if math.type and math.type(v) == "integer" then return tostring(v) end
        if v == math.floor(v) then return string.format("%d", v) end
        return string.format("%.6g", v)
    end
    if tv == "boolean" then return v and "true" or "false" end
    if tv == "table" then return json_table(v) end
    if tv == "function" then return json_string("<function>") end
    return "null"
end

-- ---------------------------------------------------------------------------
-- recording proxies: hl.dsp.window.fullscreen({ mode = "maximized" }) becomes
-- { path = "hl.dsp.window.fullscreen", args = { { mode = "maximized" } } }

local Proxy = {}
local proxy_path, proxy_args = setmetatable({}, { __mode = "k" }), setmetatable({}, { __mode = "k" })

local function new_proxy(path, args)
    local p = setmetatable({}, Proxy)
    proxy_path[p] = path
    proxy_args[p] = args
    return p
end

-- plain data for the JSON: proxies become their path (+ args), tables recurse
local function describe(v, depth)
    depth = depth or 0
    if depth > 4 then return "…" end
    local tv = type(v)
    if tv == "table" then
        if proxy_path[v] then
            local out = { path = proxy_path[v] }
            if proxy_args[v] then
                local args = {}
                for i = 1, proxy_args[v].n do args[i] = describe(proxy_args[v][i], depth + 1) end
                if #args > 0 then out.args = args end
            end
            return out
        end
        local out = {}
        for k, x in pairs(v) do
            if type(k) == "string" or type(k) == "number" then out[k] = describe(x, depth + 1) end
        end
        return out
    end
    if tv == "function" then return "<function>" end
    if tv == "nil" then return nil end
    return v
end

Proxy.__index = function(t, k)
    local child = new_proxy(proxy_path[t] .. "." .. tostring(k))
    rawset(t, k, child)
    return child
end
Proxy.__newindex = function(t, k, v) rawset(t, k, v) end
Proxy.__call = function(t, ...)
    -- a method-style call passes the proxy itself first; drop it
    local args = table.pack(...)
    if args.n >= 1 and args[1] == t then
        local rest = table.pack(table.unpack(args, 2, args.n))
        args = rest
    end
    return new_proxy(proxy_path[t], args)
end
Proxy.__tostring = function(t) return proxy_path[t] end
Proxy.__concat = function(a, b) return tostring(a) .. tostring(b) end
Proxy.__len = function() return 0 end
Proxy.__eq = function() return false end
Proxy.__lt = function() return false end
Proxy.__le = function() return false end
Proxy.__unm = function() return 0 end
Proxy.__add = function() return 0 end
Proxy.__sub = function() return 0 end
Proxy.__mul = function() return 0 end
Proxy.__div = function() return 0 end
Proxy.__mod = function() return 0 end
Proxy.__idiv = function() return 0 end
Proxy.__pow = function() return 0 end
Proxy.__close = function() end

hl = new_proxy("hl")

-- ---------------------------------------------------------------------------
-- the section a bind belongs to: the nearest short comment line above it in
-- its source file ("-- Applications", "-- Workspaces"); long comments are
-- prose, not headers, and are skipped

local file_lines = {}
local function lines_of(path)
    if file_lines[path] ~= nil then return file_lines[path] end
    local lines = {}
    local f = real_open(path, "r")
    if f then
        for line in f:lines() do lines[#lines + 1] = line end
        f:close()
    end
    file_lines[path] = lines
    return lines
end

local function section_for(source, line)
    if not source or source:sub(1, 1) ~= "@" or not line or line <= 0 then return nil end
    local lines = lines_of(source:sub(2))
    for i = line - 1, 1, -1 do
        local text = lines[i] and lines[i]:match("^%s*%-%-%s*(.-)%s*$")
        if text and text:find("%a") and #text <= 32 and not text:find("[,;:]$") then
            return text
        end
    end
    return nil
end

-- ---------------------------------------------------------------------------
-- record hl.bind(); hl.define_submap(name, fn) runs fn so the binds it
-- declares are recorded with their submap

local current_submap = ""

rawset(hl, "bind", function(keys, dispatcher, opts)
    local info = debug.getinfo(2, "Sl")
    local record = {
        keys = type(keys) == "table" and describe(keys) or tostring(keys),
        action = describe(dispatcher),
        opts = type(opts) == "table" and describe(opts) or nil,
        submap = current_submap,
        section = info and section_for(info.source, info.currentline) or nil,
        file = info and info.source and info.source:gsub("^@", "") or nil,
        line = info and info.currentline or nil,
    }
    io.stdout:write(json_value(record), "\n")
    return new_proxy("hl.bind.result")
end)

rawset(hl, "define_submap", function(name, ...)
    local previous = current_submap
    current_submap = tostring(name)
    for i = 1, select("#", ...) do
        local f = select(i, ...)
        if type(f) == "function" then pcall(f) end
    end
    current_submap = previous
    return new_proxy("hl.define_submap.result")
end)
-- unbind keeps working as a proxy (recorded binds stay listed)

-- ---------------------------------------------------------------------------
-- run the config with its directory on the module path

package.path = config_dir .. "/?.lua;" .. config_dir .. "/?/init.lua;" .. package.path

local chunk, err = loadfile(config_path)
if not chunk then
    io.stderr:write("hypr-shell keybinds: ", tostring(err), "\n")
    os.exit = nil
    return
end
local ok, run_err = pcall(chunk)
if not ok then
    io.stderr:write("hypr-shell keybinds: ", tostring(run_err), "\n")
end
io.stdout:flush()
