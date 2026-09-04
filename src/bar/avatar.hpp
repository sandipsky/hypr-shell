#pragma once

#include <gtkmm.h>

#include <string>

namespace hyprshell {

// Square avatar texture of `size` px: the user's picture (pre-scaled and
// centre-cropped so a Gtk::Picture never grows past its box), or the bundled
// fallback silhouette when `path` is empty or unreadable. Null only if even
// the resource fails to decode.
Glib::RefPtr<Gdk::Texture> load_avatar_texture(const std::string& path, int size);

} // namespace hyprshell
