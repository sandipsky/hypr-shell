// Async subprocess helper for hypr-shell-settings pages that shell out
// (nmcli, iw, ip, pkexec). GSubprocess + communicate_utf8_async; the
// callback runs on the main loop with ok = exited with status 0.
#pragma once

#include <adwaita.h>

#include <functional>
#include <string>
#include <vector>

namespace hyprshell::settings {

using CommandDone = std::function<void(bool ok, int status, const std::string& out,
                                       const std::string& err)>;

void run_command(const std::vector<std::string>& argv, CommandDone done);

std::string trim(const std::string& text);
std::string first_line(const std::string& text);
std::vector<std::string> split_lines(const std::string& text);

} // namespace hyprshell::settings
