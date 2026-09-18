#pragma once

#include <string>
#include <vector>

namespace hyprshell {

// A constexpr table (entries with a `const char* key`) in the user's order:
// the keys listed in `order` first (unknown keys dropped, duplicates
// ignored), then every entry the list does not name, in table order — so a
// partial or stale list still shows everything. Shared by the shell and
// hypr-shell-settings (session.order, desktop_menu.order) so both sides
// resolve an order identically.
template <typename Item, std::size_t N>
std::vector<const Item*> items_in_config_order(const Item (&table)[N],
                                               const std::vector<std::string>& order) {
    std::vector<const Item*> out;
    auto listed = [&](const Item* item) {
        for (const auto* i : out)
            if (i == item)
                return true;
        return false;
    };
    for (const auto& key : order)
        for (const auto& item : table)
            if (key == item.key && !listed(&item))
                out.push_back(&item);
    for (const auto& item : table)
        if (!listed(&item))
            out.push_back(&item);
    return out;
}

} // namespace hyprshell
