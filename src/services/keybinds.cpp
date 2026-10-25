#include "services/keybinds.hpp"

#include "services/apps.hpp"
#include "services/hyprland.hpp"
#include "services/settings_pages.hpp"

#include <giomm.h>
#include <glibmm.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>

using json = nlohmann::json;

namespace hyprshell {

namespace {

constexpr const char* kStubResource = "/dev/hyprshell/Shell/keybinds-introspect.lua";
constexpr unsigned kTimeoutMs = 3000; // both sources answer in milliseconds; this is the fallback

std::string lowercase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string uppercase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
    return s;
}

std::string trim(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return "";
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

std::string capitalize(std::string s) {
    if (!s.empty())
        s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

// -- keys ---------------------------------------------------------------------

// Hyprland's modifier names → mask bits (KeybindManager::stringToModMask)
unsigned mod_bit(const std::string& name) {
    const std::string n = uppercase(name);
    if (n == "SHIFT")
        return 1;
    if (n == "CAPS")
        return 2;
    if (n == "CTRL" || n == "CONTROL")
        return 4;
    if (n == "ALT" || n == "MOD1")
        return 8;
    if (n == "MOD2")
        return 16;
    if (n == "MOD3")
        return 32;
    if (n == "SUPER" || n == "WIN" || n == "LOGO" || n == "MOD4")
        return 64;
    if (n == "MOD5")
        return 128;
    return 0;
}

std::vector<std::string> mod_names(unsigned mask) {
    std::vector<std::string> out;
    if (mask & 64)
        out.push_back("Super");
    if (mask & 4)
        out.push_back("Ctrl");
    if (mask & 8)
        out.push_back("Alt");
    if (mask & 1)
        out.push_back("Shift");
    if (mask & 2)
        out.push_back("Caps");
    if (mask & 16)
        out.push_back("Mod2");
    if (mask & 32)
        out.push_back("Mod3");
    if (mask & 128)
        out.push_back("Mod5");
    return out;
}

// "SUPER + SHIFT + E" (hl.bind's key string) → mask 65, key "e"
struct ParsedKeys {
    unsigned modmask = 0;
    std::string key_lc;
};

ParsedKeys parse_keys(const std::string& text) {
    std::vector<std::string> tokens;
    std::string current;
    for (char c : text) {
        if (c == '+') {
            tokens.push_back(trim(current));
            current.clear();
        } else {
            current += c;
        }
    }
    tokens.push_back(trim(current));
    ParsedKeys out;
    if (tokens.size() >= 2 && tokens.back().empty())
        tokens.back() = "plus"; // "SUPER + +": the key is the plus sign itself
    out.key_lc = lowercase(tokens.back());
    for (std::size_t i = 0; i + 1 < tokens.size(); ++i)
        out.modmask |= mod_bit(tokens[i]);
    return out;
}

// "AudioRaiseVolume" → "Audio raise volume"
std::string split_camel(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (i > 0 && std::isupper(c) && std::islower(static_cast<unsigned char>(s[i - 1]))) {
            out += ' ';
            out += static_cast<char>(std::tolower(c));
        } else {
            out += i == 0 ? static_cast<char>(c) : static_cast<char>(std::tolower(c));
        }
    }
    return out;
}

std::string pretty_key(const std::string& key, int keycode, bool catch_all) {
    if (catch_all)
        return "Any key";
    if (key.empty())
        return keycode > 0 ? "Keycode " + std::to_string(keycode) : "?";
    const std::string lc = lowercase(key);
    static constexpr std::pair<const char*, const char*> kNames[] = {
        {"return", "Enter"},            {"kp_enter", "Numpad Enter"},   {"space", "Space"},
        {"tab", "Tab"},                 {"escape", "Esc"},              {"backspace", "Backspace"},
        {"delete", "Del"},              {"insert", "Insert"},           {"left", "←"},
        {"right", "→"},                 {"up", "↑"},                    {"down", "↓"},
        {"home", "Home"},               {"end", "End"},                 {"prior", "Page Up"},
        {"page_up", "Page Up"},         {"next", "Page Down"},          {"page_down", "Page Down"},
        {"print", "Print"},             {"menu", "Menu"},               {"pause", "Pause"},
        {"scroll_lock", "Scroll Lock"}, {"caps_lock", "Caps Lock"},     {"num_lock", "Num Lock"},
        {"comma", ","},                 {"period", "."},                {"slash", "/"},
        {"backslash", "\\"},            {"semicolon", ";"},             {"apostrophe", "'"},
        {"grave", "`"},                 {"minus", "-"},                 {"equal", "="},
        {"plus", "+"},                  {"bracketleft", "["},           {"bracketright", "]"},
        {"mouse:272", "Left click"},    {"mouse:273", "Right click"},   {"mouse:274", "Middle click"},
        {"mouse:275", "Mouse back"},    {"mouse:276", "Mouse forward"}, {"mouse_down", "Scroll down"},
        {"mouse_up", "Scroll up"},      {"mouse_left", "Scroll left"},  {"mouse_right", "Scroll right"},
        {"xf86audioraisevolume", "Volume up"},   {"xf86audiolowervolume", "Volume down"},
        {"xf86audiomute", "Mute"},               {"xf86audiomicmute", "Mic mute"},
        {"xf86monbrightnessup", "Brightness up"}, {"xf86monbrightnessdown", "Brightness down"},
        {"xf86audioplay", "Play"},               {"xf86audiopause", "Pause"},
        {"xf86audionext", "Next track"},         {"xf86audioprev", "Previous track"},
        {"xf86audiostop", "Stop"},               {"xf86screensaver", "Screen saver"},
        {"xf86poweroff", "Power"},               {"xf86sleep", "Sleep"},
        {"xf86calculator", "Calculator"},        {"xf86wlan", "Wi-Fi"},
        {"xf86display", "Display"},              {"xf86kbdbrightnessup", "Keyboard light up"},
        {"xf86kbdbrightnessdown", "Keyboard light down"}, {"xf86touchpadtoggle", "Touchpad"},
        {"xf86search", "Search"},                {"xf86mail", "Mail"},
        {"xf86homepage", "Home page"},           {"xf86explorer", "Files"},
        {"xf86tools", "Tools"},                  {"xf86favorites", "Favorites"},
    };
    for (const auto& [name, pretty] : kNames)
        if (lc == name)
            return pretty;
    if (lc.rfind("switch:", 0) == 0) // "switch:Lid Switch"
        return capitalize(lowercase(trim(key.substr(7))));
    if (lc.rfind("code:", 0) == 0)
        return "Keycode " + key.substr(5);
    if (lc.rfind("mouse:", 0) == 0)
        return "Mouse button " + key.substr(6);
    if (lc.rfind("xf86", 0) == 0)
        return split_camel(key.substr(4));
    if (lc.rfind("kp_", 0) == 0)
        return "Numpad " + capitalize(key.substr(3));
    if (key.size() == 1)
        return uppercase(key);
    if (lc.size() <= 3 && lc[0] == 'f' && std::all_of(lc.begin() + 1, lc.end(), ::isdigit))
        return uppercase(key);
    std::string out = key;
    std::replace(out.begin(), out.end(), '_', ' ');
    return capitalize(out);
}

// -- descriptions ---------------------------------------------------------------

std::string direction_word(const std::string& d) {
    const std::string lc = lowercase(d);
    if (lc == "l" || lc == "left")
        return "left";
    if (lc == "r" || lc == "right")
        return "right";
    if (lc == "u" || lc == "up" || lc == "t" || lc == "top")
        return "up";
    if (lc == "d" || lc == "down" || lc == "b" || lc == "bottom")
        return "down";
    return lc;
}

// a workspace selector as it reads in a sentence ("workspace 3", "the next
// workspace", "special workspace scratch")
std::string workspace_phrase(const std::string& selector) {
    const std::string s = lowercase(trim(selector));
    if (s.empty())
        return "workspace";
    if (std::all_of(s.begin(), s.end(), ::isdigit))
        return "workspace " + s;
    if (s == "+1" || s == "e+1" || s == "r+1" || s == "m+1" || s == "next")
        return "the next workspace";
    if (s == "-1" || s == "e-1" || s == "r-1" || s == "m-1" || s == "prev")
        return "the previous workspace";
    if (s == "previous" || s == "previous_per_monitor")
        return "the last used workspace";
    if (s == "empty" || s.rfind("empty", 0) == 0)
        return "an empty workspace";
    if (s == "special")
        return "the special workspace";
    if (s.rfind("special:", 0) == 0)
        return "special workspace " + s.substr(8);
    if (s.rfind("name:", 0) == 0)
        return "workspace " + s.substr(5);
    return "workspace " + s;
}

// the settings page a `HS_SETTINGS_PAGE=<tag>` environment prefix opens
std::string settings_page_title(const std::string& tag) {
    for (const auto& page : kSettingsPages)
        if (tag == page.name)
            return page.title;
    return "";
}

// first token of a shell command, skipping `env` and VAR=value assignments
std::string command_program(const std::string& cmd, std::string* rest, std::string* settings_page) {
    std::vector<std::string> tokens;
    std::string current;
    char quote = 0;
    for (char c : cmd) {
        if (quote) {
            if (c == quote)
                quote = 0;
            else
                current += c;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty())
                tokens.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty())
        tokens.push_back(current);
    std::size_t i = 0;
    while (i < tokens.size()) {
        const auto eq = tokens[i].find('=');
        if (tokens[i] == "env") {
            ++i;
        } else if (eq != std::string::npos && eq > 0 && tokens[i].find('/') > eq) {
            if (tokens[i].rfind("HS_SETTINGS_PAGE=", 0) == 0 && settings_page)
                *settings_page = tokens[i].substr(eq + 1);
            ++i;
        } else {
            break;
        }
    }
    if (i >= tokens.size())
        return "";
    std::string program = tokens[i];
    if (const auto slash = program.rfind('/'); slash != std::string::npos)
        program = program.substr(slash + 1);
    if (rest) {
        rest->clear();
        for (std::size_t j = i + 1; j < tokens.size(); ++j)
            *rest += (rest->empty() ? "" : " ") + tokens[j];
    }
    return program;
}

std::string humanize_command(const std::string& command) {
    const std::string cmd = trim(command);
    if (cmd.empty())
        return "Run a command";
    std::string rest, settings_page;
    const std::string program = command_program(cmd, &rest, &settings_page);
    const std::string rest_lc = lowercase(rest);
    const std::string cmd_lc = lowercase(cmd);

    if (program == "hypr-shell") {
        static constexpr std::pair<const char*, const char*> kFlags[] = {
            {"--launcher", "Open the launcher"},          {"--clipboard", "Clipboard history"},
            {"--app-menu", "Open the app menu"},          {"--session", "Session menu"},
            {"--lock-and-suspend", "Lock and suspend"},   {"--lock", "Lock the screen"},
            {"--control-center", "Control center"},       {"--wallpaper-next", "Next wallpaper"},
            {"--keybindings", "Keyboard shortcuts"},
        };
        for (const auto& [flag, label] : kFlags)
            if (rest_lc.find(flag) != std::string::npos)
                return label;
        return "hypr-shell " + rest;
    }
    if (program == "hypr-shell-settings") {
        const std::string title = settings_page_title(settings_page);
        return title.empty() ? "Open Settings" : "Settings: " + title;
    }
    if (program == "wpctl") {
        if (rest_lc.find("set-mute") != std::string::npos)
            return rest_lc.find("source") != std::string::npos ? "Toggle microphone mute"
                                                                : "Toggle mute";
        if (rest_lc.find("set-volume") != std::string::npos) {
            const bool mic = rest_lc.find("source") != std::string::npos;
            if (rest_lc.find("%+") != std::string::npos || rest_lc.find("+") != std::string::npos)
                return mic ? "Microphone volume up" : "Volume up";
            if (rest_lc.find("%-") != std::string::npos || rest_lc.find("-") != std::string::npos)
                return mic ? "Microphone volume down" : "Volume down";
            return "Set volume";
        }
    }
    if (program == "pactl" || program == "pamixer" || program == "amixer") {
        if (cmd_lc.find("mute") != std::string::npos)
            return "Toggle mute";
        if (cmd_lc.find('+') != std::string::npos)
            return "Volume up";
        if (cmd_lc.find('-') != std::string::npos)
            return "Volume down";
    }
    if (program == "brightnessctl" || program == "light" || program == "ddcutil") {
        if (rest_lc.find('+') != std::string::npos || rest_lc.find(" -a ") != std::string::npos)
            return "Brightness up";
        if (rest_lc.find('-') != std::string::npos)
            return "Brightness down";
        return "Set brightness";
    }
    if (program == "playerctl") {
        if (rest_lc.find("play-pause") != std::string::npos)
            return "Play / pause";
        if (rest_lc.find("next") != std::string::npos)
            return "Next track";
        if (rest_lc.find("previous") != std::string::npos)
            return "Previous track";
        if (rest_lc.find("stop") != std::string::npos)
            return "Stop playback";
    }
    if (program == "hyprctl" && rest_lc.rfind("reload", 0) == 0)
        return "Reload the Hyprland config";
    if (program == "grim" || program == "hyprshot" || program == "flameshot" || program == "grimblast")
        return cmd_lc.find("slurp") != std::string::npos || cmd_lc.find("region") != std::string::npos ||
                       cmd_lc.find(" -m region") != std::string::npos
                   ? "Screenshot a region"
                   : "Take a screenshot";
    if (program == "hyprpicker")
        return "Pick a colour from the screen";
    if (program == "hyprlock" || program == "swaylock" ||
        (program == "loginctl" && rest_lc.find("lock") != std::string::npos))
        return "Lock the screen";
    if (program == "wlogout")
        return "Session menu";
    if (program == "systemctl" || program == "loginctl") {
        if (rest_lc.find("suspend") != std::string::npos)
            return "Suspend";
        if (rest_lc.find("poweroff") != std::string::npos)
            return "Power off";
        if (rest_lc.find("reboot") != std::string::npos)
            return "Reboot";
    }
    if (program == "notify-send")
        return "Send a notification";

    // an installed application: its display name (desktop entry Exec basename)
    const std::string program_lc = lowercase(program);
    for (const auto& app : Apps::get().entries()) {
        if (lowercase(app.exec_name) != program_lc)
            continue;
        // a URI / path argument is worth showing; flags are not
        if (!rest.empty() && rest[0] != '-' && rest.size() <= 40)
            return "Open " + app.name + " (" + rest + ")";
        return "Open " + app.name;
    }
    std::string shown = program + (rest.empty() ? "" : " " + rest);
    if (shown.size() > 64)
        shown = shown.substr(0, 61) + "…";
    return "Run " + shown;
}

std::string json_text(const json& v) {
    if (v.is_string())
        return v.get<std::string>();
    if (v.is_number_integer())
        return std::to_string(v.get<long long>());
    if (v.is_number())
        return Glib::ustring::format(v.get<double>());
    if (v.is_boolean())
        return v.get<bool>() ? "true" : "false";
    return "";
}

// "k = v, k2 = v2" for the generic fallback
std::string summarize_args(const json& args) {
    std::string out;
    for (const auto& arg : args) {
        std::string piece;
        if (arg.is_object()) {
            for (const auto& [k, v] : arg.items())
                piece += (piece.empty() ? "" : ", ") + k + " = " + json_text(v);
        } else if (arg.is_object() && arg.contains("path")) {
            piece = arg["path"].get<std::string>();
        } else {
            piece = json_text(arg);
        }
        if (!piece.empty())
            out += (out.empty() ? "" : "; ") + piece;
    }
    return out;
}

// the recorded dispatcher of a Lua bind → a sentence
std::string humanize_action(const json& action) {
    if (action.is_string())
        return action.get<std::string>() == "<function>" ? "Lua function" : action.get<std::string>();
    if (!action.is_object() || !action.contains("path") || !action["path"].is_string())
        return "";
    std::string path = action["path"].get<std::string>();
    if (path.rfind("hl.dsp.", 0) == 0)
        path = path.substr(7);
    else if (path.rfind("hl.", 0) == 0)
        path = path.substr(3);
    const json args = action.value("args", json::array());
    const json a0 = args.is_array() && !args.empty() ? args[0] : json();
    auto field = [&](const char* key) -> std::string {
        return a0.is_object() && a0.contains(key) ? json_text(a0[key]) : "";
    };
    const std::string str0 = a0.is_string() ? a0.get<std::string>() : "";

    if (path == "exec_cmd" || path == "exec_raw" || path == "exec")
        return humanize_command(str0);
    if (path == "exit")
        return "Exit Hyprland";
    if (path == "window.close")
        return "Close window";
    if (path == "window.kill")
        return "Kill window";
    if (path == "window.cycle_next")
        return "Focus the next window";
    if (path == "window.cycle_prev")
        return "Focus the previous window";
    if (path == "window.center")
        return "Center window";
    if (path == "window.pin")
        return "Pin window";
    if (path == "window.pseudo")
        return "Toggle pseudo-tiling";
    if (path == "window.bring_to_top" || path == "window.alter_zorder")
        return "Bring window to the top";
    if (path == "window.toggle_swallow")
        return "Toggle window swallowing";
    if (path == "window.drag")
        return "Drag window";
    if (path == "window.fullscreen" || path == "window.fullscreen_state") {
        const std::string mode = lowercase(field("mode"));
        const std::string act = lowercase(field("action"));
        const std::string what = mode == "maximized" || mode == "maximize" ? "maximized" : "fullscreen";
        if (act == "on" || act == "enable" || act == "enter")
            return "Make window " + what;
        if (act == "off" || act == "disable" || act == "exit")
            return "Leave " + what;
        return "Toggle " + what;
    }
    if (path == "window.float") {
        const std::string act = lowercase(field("action"));
        if (act == "on" || act == "enable")
            return "Float window";
        if (act == "off" || act == "disable")
            return "Tile window";
        return "Toggle floating";
    }
    if (path == "window.move") {
        if (!field("direction").empty())
            return "Move window " + direction_word(field("direction"));
        if (!field("workspace").empty())
            return "Move window to " + workspace_phrase(field("workspace"));
        if (!field("monitor").empty())
            return "Move window to monitor " + field("monitor");
        if (!field("x").empty() || !field("y").empty())
            return "Move window by " + (field("x").empty() ? "0" : field("x")) + " × " +
                   (field("y").empty() ? "0" : field("y"));
        return "Move window";
    }
    if (path == "window.resize") {
        if (!field("x").empty() || !field("y").empty())
            return std::string(lowercase(field("relative")) == "true" || field("exact").empty()
                                   ? "Resize window by "
                                   : "Resize window to ") +
                   (field("x").empty() ? "0" : field("x")) + " × " +
                   (field("y").empty() ? "0" : field("y"));
        return "Resize window";
    }
    if (path == "window.swap")
        return "Swap window " + direction_word(field("direction"));
    if (path == "focus") {
        if (!field("workspace").empty())
            return "Switch to " + workspace_phrase(field("workspace"));
        if (!field("direction").empty())
            return "Focus window " + direction_word(field("direction"));
        if (!field("monitor").empty())
            return "Focus monitor " + field("monitor");
        if (!field("urgent").empty())
            return "Focus the urgent window";
        return "Focus window";
    }
    if (path == "workspace.toggle_special")
        return field("name").empty() && str0.empty()
                   ? "Toggle the special workspace"
                   : "Toggle special workspace " + (str0.empty() ? field("name") : str0);
    if (path == "workspace.move")
        return "Move workspace to another monitor";
    if (path == "workspace.rename")
        return "Rename workspace";
    if (path == "workspace.swap_monitors")
        return "Swap workspaces between monitors";
    if (path == "workspace.change_id")
        return "Change workspace id";
    if (path == "group.toggle")
        return "Toggle window group";
    if (path == "group.next")
        return "Next window in group";
    if (path == "group.prev")
        return "Previous window in group";
    if (path == "group.lock" || path == "group.lock_active")
        return "Lock window group";
    if (path == "group.move_window")
        return "Move window out of group";
    if (path == "group.active")
        return "Focus window in group";
    if (path == "cursor.move_to_corner")
        return "Move cursor to a corner";
    if (path == "cursor.move")
        return "Move cursor";
    if (path == "submap")
        return str0.empty() ? "Leave submap" : "Enter submap “" + str0 + "”";
    if (path == "pass")
        return "Pass the key to the window";
    if (path == "send_shortcut")
        return "Send a shortcut to the window";
    if (path == "global")
        return "Global shortcut";
    if (path == "force_renderer_reload")
        return "Reload the renderer";
    if (path == "dpms")
        return lowercase(field("action")) == "off" ? "Turn screens off"
               : lowercase(field("action")) == "on" ? "Turn screens on"
                                                    : "Toggle screen power";
    if (path == "no_op")
        return "Do nothing";

    // generic: "window.set_prop" → "Window: set prop (k = v)"
    std::string out = path;
    std::string ns;
    if (const auto dot = out.find('.'); dot != std::string::npos) {
        ns = capitalize(out.substr(0, dot)) + ": ";
        out = out.substr(dot + 1);
    }
    std::replace(out.begin(), out.end(), '_', ' ');
    out = ns + (ns.empty() ? capitalize(out) : out);
    const std::string summary = summarize_args(args);
    return summary.empty() ? out : out + " (" + summary + ")";
}

// a text-config dispatcher (hyprland.conf on 0.56, `hyprctl binds` shows the
// real names there) → a sentence
std::string humanize_text_dispatcher(const std::string& dispatcher, const std::string& arg) {
    const std::string d = lowercase(dispatcher);
    if (d == "exec" || d == "execr")
        return humanize_command(arg);
    if (d == "killactive")
        return "Close window";
    if (d == "forcekillactive")
        return "Kill window";
    if (d == "togglefloating")
        return "Toggle floating";
    if (d == "fullscreen")
        return arg == "1" ? "Toggle maximized" : "Toggle fullscreen";
    if (d == "workspace")
        return "Switch to " + workspace_phrase(arg);
    if (d == "movetoworkspace" || d == "movetoworkspacesilent")
        return "Move window to " + workspace_phrase(arg);
    if (d == "movefocus")
        return "Focus window " + direction_word(arg);
    if (d == "movewindow")
        return "Move window " + direction_word(arg);
    if (d == "swapwindow")
        return "Swap window " + direction_word(arg);
    if (d == "togglespecialworkspace")
        return arg.empty() ? "Toggle the special workspace" : "Toggle special workspace " + arg;
    if (d == "exit")
        return "Exit Hyprland";
    if (d == "pseudo")
        return "Toggle pseudo-tiling";
    if (d == "togglesplit")
        return "Toggle split direction";
    if (d == "cyclenext")
        return "Focus the next window";
    if (d == "pin")
        return "Pin window";
    if (d == "centerwindow")
        return "Center window";
    if (d == "submap")
        return arg == "reset" ? "Leave submap" : "Enter submap “" + arg + "”";
    return capitalize(dispatcher) + (arg.empty() ? "" : " " + arg);
}

// -- merge ----------------------------------------------------------------------

struct StubRecord {
    unsigned modmask = 0;
    std::string key_lc;
    std::string submap;
    std::string description;
    std::string section;
    bool used = false;
};

std::vector<StubRecord> parse_stub(const std::string& output) {
    std::vector<StubRecord> records;
    std::size_t start = 0;
    while (start < output.size()) {
        auto end = output.find('\n', start);
        if (end == std::string::npos)
            end = output.size();
        const std::string line = output.substr(start, end - start);
        start = end + 1;
        if (line.empty())
            continue;
        const json record = json::parse(line, nullptr, false);
        if (record.is_discarded() || !record.is_object() || !record.contains("keys") ||
            !record["keys"].is_string())
            continue;
        StubRecord r;
        const auto parsed = parse_keys(record["keys"].get<std::string>());
        r.modmask = parsed.modmask;
        r.key_lc = parsed.key_lc;
        r.submap = record.value("submap", "");
        r.section = record.value("section", "");
        if (record.contains("opts") && record["opts"].is_object()) {
            const json& opts = record["opts"];
            if (opts.contains("description") && opts["description"].is_string())
                r.description = opts["description"].get<std::string>();
            else if (opts.contains("desc") && opts["desc"].is_string())
                r.description = opts["desc"].get<std::string>();
        }
        if (r.description.empty() && record.contains("action"))
            r.description = humanize_action(record["action"]);
        records.push_back(std::move(r));
    }
    return records;
}

void finish_bind(Keybind& bind) {
    std::string haystack;
    for (const auto& m : bind.mods)
        haystack += m + " ";
    haystack += bind.key + " " + bind.description + " " + bind.group + " " + bind.submap;
    bind.search_text = lowercase(haystack);
}

std::vector<Keybind> merge(const std::string& binds_reply, const std::string& stub_output) {
    std::vector<StubRecord> records = parse_stub(stub_output);
    std::vector<Keybind> out;

    const json binds = json::parse(binds_reply, nullptr, false);
    if (!binds.is_array()) {
        // no j/binds (Hyprland unreachable): show what the config declares
        for (auto& r : records) {
            Keybind b;
            b.mods = mod_names(r.modmask);
            b.key = pretty_key(r.key_lc, 0, false);
            b.description = r.description.empty() ? "Lua action" : r.description;
            b.submap = r.submap;
            b.group = !r.section.empty() ? r.section
                      : !r.submap.empty()  ? "Submap: " + r.submap
                                           : "Other";
            finish_bind(b);
            out.push_back(std::move(b));
        }
        return out;
    }

    for (const auto& entry : binds) {
        if (!entry.is_object())
            continue;
        const unsigned modmask = static_cast<unsigned>(entry.value("modmask", 0));
        const std::string key = entry.value("key", "");
        const std::string key_lc = lowercase(key);
        const std::string submap = entry.value("submap", "");
        StubRecord* match = nullptr;
        for (auto& r : records)
            if (!r.used && r.modmask == modmask && r.key_lc == key_lc && r.submap == submap) {
                match = &r;
                break;
            }
        if (match)
            match->used = true;

        Keybind b;
        b.mods = mod_names(modmask);
        b.key = pretty_key(key, entry.value("keycode", 0), entry.value("catch_all", false));
        b.submap = submap;
        b.mouse = entry.value("mouse", false);
        b.locked = entry.value("locked", false);
        const std::string dispatcher = entry.value("dispatcher", "");
        const std::string arg = entry.value("arg", "");
        if (entry.value("has_description", false) && !entry.value("description", "").empty())
            b.description = entry.value("description", "");
        else if (match && !match->description.empty())
            b.description = match->description;
        else if (!dispatcher.empty() && dispatcher != "__lua")
            b.description = humanize_text_dispatcher(dispatcher, arg);
        else
            b.description = "Lua action";
        b.group = match && !match->section.empty() ? match->section
                  : !submap.empty()                 ? "Submap: " + submap
                                                    : "Other";
        finish_bind(b);
        out.push_back(std::move(b));
    }
    return out;
}

// -- sources --------------------------------------------------------------------

std::string config_path() {
    const std::string path = Glib::build_filename(Glib::get_user_config_dir(), "hypr", "hyprland.lua");
    return Glib::file_test(path, Glib::FileTest::IS_REGULAR) ? path : "";
}

std::string lua_program() {
    for (const char* name : {"lua", "lua5.4", "lua5.5"}) {
        const std::string found = Glib::find_program_in_path(name);
        if (!found.empty())
            return found;
    }
    return "";
}

struct Pending {
    std::function<void(std::vector<Keybind>)> on_done;
    std::optional<std::string> binds;
    std::optional<std::string> stub;
    bool finished = false;
    sigc::connection timeout;
    Glib::RefPtr<Gio::Subprocess> process;
};

void maybe_finish(const std::shared_ptr<Pending>& p, bool force) {
    if (p->finished || (!force && (!p->binds || !p->stub)))
        return;
    p->finished = true;
    p->timeout.disconnect();
    if (p->process && !p->stub)
        p->process->force_exit();
    auto binds = merge(p->binds.value_or(""), p->stub.value_or(""));
    if (p->on_done)
        p->on_done(std::move(binds));
}

void run_stub(const std::shared_ptr<Pending>& p) {
    const std::string lua = lua_program();
    const std::string config = config_path();
    if (lua.empty() || config.empty()) {
        p->stub = "";
        return;
    }
    Glib::RefPtr<const Glib::Bytes> script;
    try {
        script = Gio::Resource::lookup_data_global(kStubResource);
    } catch (const Glib::Error& e) {
        g_warning("keybinds: introspection script missing: %s", e.what());
        p->stub = "";
        return;
    }
    try {
        p->process = Gio::Subprocess::create({lua, "-", config},
                                             Gio::Subprocess::Flags::STDIN_PIPE |
                                                 Gio::Subprocess::Flags::STDOUT_PIPE |
                                                 Gio::Subprocess::Flags::STDERR_SILENCE);
    } catch (const Glib::Error& e) {
        g_warning("keybinds: cannot run lua: %s", e.what());
        p->stub = "";
        return;
    }
    auto process = p->process;
    process->communicate_async(script, [p, process](Glib::RefPtr<Gio::AsyncResult>& result) {
        std::string out;
        try {
            auto [stdout_bytes, stderr_bytes] = process->communicate_finish(result);
            if (stdout_bytes) {
                gsize size = 0;
                const auto* data = static_cast<const char*>(stdout_bytes->get_data(size));
                out.assign(data, size);
            }
        } catch (const Glib::Error& e) {
            g_warning("keybinds: config replay failed: %s", e.what());
        }
        p->process.reset();
        p->stub = out;
        maybe_finish(p, false);
    });
}

} // namespace

bool keybinds_introspection_available() {
    return !lua_program().empty() && !config_path().empty();
}

void fetch_keybinds(std::function<void(std::vector<Keybind>)> on_done) {
    auto p = std::make_shared<Pending>();
    p->on_done = std::move(on_done);
    if (Hyprland::get().available()) {
        Hyprland::get().request("j/binds", [p](const std::string& reply) {
            p->binds = reply;
            maybe_finish(p, false);
        });
    } else {
        p->binds = "";
    }
    run_stub(p);
    // a failed request never calls back; a runaway config never exits
    p->timeout = Glib::signal_timeout().connect(
        [p] {
            maybe_finish(p, true);
            return false;
        },
        kTimeoutMs);
    maybe_finish(p, false);
}

} // namespace hyprshell
