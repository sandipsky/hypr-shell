#include "bar/avatar.hpp"

#include <gdk-pixbuf/gdk-pixbuf.h>

#include <algorithm>

namespace hyprshell {

namespace {

constexpr const char* kFallbackResource = "/dev/hyprshell/Shell/avatar-fallback.svg";

Glib::RefPtr<Gdk::Texture> square_texture(const Glib::RefPtr<Gdk::Pixbuf>& pixbuf, int size) {
    const int w = pixbuf->get_width(), h = pixbuf->get_height();
    const int side = std::min({w, h, size});
    return Gdk::Texture::create_for_pixbuf(
        Gdk::Pixbuf::create_subpixbuf(pixbuf, (w - side) / 2, (h - side) / 2, side, side));
}

} // namespace

Glib::RefPtr<Gdk::Texture> load_avatar_texture(const std::string& path, int size) {
    if (!path.empty()) {
        try {
            int image_w = 0, image_h = 0;
            if (gdk_pixbuf_get_file_info(path.c_str(), &image_w, &image_h) != nullptr && image_w > 0 &&
                image_h > 0) {
                auto pixbuf = image_w >= image_h ? Gdk::Pixbuf::create_from_file(path, -1, size, true)
                                                 : Gdk::Pixbuf::create_from_file(path, size, -1, true);
                if (pixbuf)
                    return square_texture(pixbuf, size);
            }
            g_warning("avatar: cannot read %s — using the fallback picture", path.c_str());
        } catch (const Glib::Error& e) {
            g_warning("avatar: cannot load %s (%s) — using the fallback picture", path.c_str(), e.what());
        }
    }
    try {
        return square_texture(Gdk::Pixbuf::create_from_resource(kFallbackResource, size, size, true), size);
    } catch (const Glib::Error& e) {
        g_warning("avatar: fallback picture failed: %s", e.what());
        return {};
    }
}

} // namespace hyprshell
