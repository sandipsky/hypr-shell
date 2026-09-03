#pragma once

namespace hyprshell {

// Icon choices for the app menu bar module (bar.app_menu.icon), shared by the
// shell and hypr-shell-settings so the dropdown and the renderer agree.
// Presets are noctalia-tabler-icons glyphs (\u escapes — never literal PUA);
// the two special keys below render a themed icon / image instead.
struct AppMenuIconPreset {
    const char* key;   // config value
    const char* label; // settings dropdown text
    const char* glyph;
};

constexpr AppMenuIconPreset kAppMenuIconPresets[] = {
    {"rocket", "Rocket", "\uEC45"}, // Noctalia's launcher widget default
    {"apps", "Apps", "\uEBB6"},
    {"layout-grid", "Grid", "\uEDBA"},
    {"grid-dots", "Dot grid", "\uEABA"},
    {"menu-2", "Menu", "\uEC42"},
    {"category", "Category", "\uF1F6"},
    {"hexagon", "Hexagon", "\uEC02"},
    {"planet", "Planet", "\uEC08"},
    {"sparkles", "Sparkles", "\uF6D7"},
    {"command", "Command", "\uEA78"},
    {"home", "Home", "\uEAC1"},
    {"search", "Search", "\uEB1C"},
};

// "distro": the /etc/os-release LOGO icon from the icon theme (Noctalia's
// useDistroLogo). "custom": bar.app_menu.custom_icon names a themed icon or
// an image file (Noctalia's customIconPath).
constexpr const char* kAppMenuIconDistro = "distro";
constexpr const char* kAppMenuIconCustom = "custom";

} // namespace hyprshell
