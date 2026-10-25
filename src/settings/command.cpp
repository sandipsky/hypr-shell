#include "settings/command.hpp"

namespace hyprshell::settings {

void run_command(const std::vector<std::string>& argv, CommandDone done) {
    std::vector<const char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (const auto& a : argv)
        cargv.push_back(a.c_str());
    cargv.push_back(nullptr);

    GError* error = nullptr;
    GSubprocess* proc = g_subprocess_newv(
        cargv.data(),
        static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                      G_SUBPROCESS_FLAGS_STDERR_PIPE),
        &error);
    if (proc == nullptr) {
        const std::string message = error != nullptr ? error->message : "spawn failed";
        g_clear_error(&error);
        done(false, -1, "", message);
        return;
    }
    auto* cb = new CommandDone(std::move(done));
    g_subprocess_communicate_utf8_async(
        proc, nullptr, nullptr,
        +[](GObject* source, GAsyncResult* result, gpointer data) {
            auto* cb = static_cast<CommandDone*>(data);
            GSubprocess* proc = G_SUBPROCESS(source);
            gchar* out = nullptr;
            gchar* err = nullptr;
            GError* error = nullptr;
            const bool finished =
                g_subprocess_communicate_utf8_finish(proc, result, &out, &err, &error);
            int status = -1;
            if (finished && g_subprocess_get_if_exited(proc))
                status = g_subprocess_get_exit_status(proc);
            std::string out_s = out != nullptr ? out : "";
            std::string err_s = err != nullptr ? err : "";
            if (!finished && error != nullptr)
                err_s = error->message;
            (*cb)(finished && status == 0, status, out_s, err_s);
            delete cb;
            g_free(out);
            g_free(err);
            g_clear_error(&error);
            g_object_unref(proc);
        },
        cb);
}

std::string trim(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return "";
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

std::string first_line(const std::string& text) {
    const auto nl = text.find('\n');
    return trim(nl == std::string::npos ? text : text.substr(0, nl));
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        if (end > start)
            lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

} // namespace hyprshell::settings
