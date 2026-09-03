#include "services/pam_auth.hpp"

#include <security/pam_appl.h>

#include <cstdlib>
#include <cstring>

namespace hyprshell {

// Queue an event for the main loop; the caller holds shared.mutex.
void PamAuth::post(Shared& shared, Event event) {
    shared.events.push_back(std::move(event));
    if (shared.dispatcher != nullptr)
        shared.dispatcher->emit(); // thread-safe: writes to a pipe
}

PamAuth::PamAuth() : shared_(std::make_shared<Shared>()) {
    shared_->dispatcher = &dispatcher_;
    dispatcher_.connect(sigc::mem_fun(*this, &PamAuth::drain_events));
}

PamAuth::~PamAuth() {
    {
        std::lock_guard lock(shared_->mutex);
        shared_->abandoned = true;
        shared_->dispatcher = nullptr; // the worker must not touch the dying dispatcher
    }
    shared_->cv.notify_all();
    // A transaction may still be inside pam_authenticate (pam_unix delays a
    // failed attempt); it finishes on its own with the shared state kept alive.
    if (thread_.joinable())
        thread_.detach();
}

std::string PamAuth::detect_service() {
    if (const char* env = g_getenv("HS_PAM_SERVICE"); env != nullptr && *env != '\0')
        return env;
    for (const char* candidate : {"login", "system-auth", "common-auth"}) {
        if (Glib::file_test(std::string("/etc/pam.d/") + candidate, Glib::FileTest::EXISTS))
            return candidate;
    }
    return "login";
}

void PamAuth::start(const std::string& service, const std::string& user,
                    const std::string& password) {
    if (active_)
        return;
    active_ = true;
    waiting_ = false;
    {
        std::lock_guard lock(shared_->mutex);
        shared_->password = password;
        shared_->password_used = false;
        shared_->response.clear();
        shared_->has_response = false;
        shared_->events.clear();
    }
    if (thread_.joinable())
        thread_.join(); // the previous transaction already posted Completed
    thread_ = std::thread(&PamAuth::run, shared_, service, user);
}

void PamAuth::respond(const std::string& password) {
    if (!active_ || !waiting_)
        return;
    waiting_ = false;
    {
        std::lock_guard lock(shared_->mutex);
        shared_->response = password;
        shared_->has_response = true;
    }
    shared_->cv.notify_all();
}

// Worker thread: the PAM conversation. Prompts are answered from the initial
// password once, then block until respond() (or destruction) provides one.
int PamAuth::conversation(int count, const ::pam_message** messages,
                          ::pam_response** responses, void* data) {
    auto* shared = static_cast<Shared*>(data);
    if (count <= 0 || messages == nullptr || responses == nullptr)
        return PAM_CONV_ERR;
    auto* replies = static_cast<pam_response*>(
        calloc(static_cast<std::size_t>(count), sizeof(pam_response)));
    if (replies == nullptr)
        return PAM_BUF_ERR;

    auto fail = [&] {
        for (int i = 0; i < count; ++i)
            free(replies[i].resp);
        free(replies);
        return PAM_CONV_ERR;
    };

    for (int i = 0; i < count; ++i) {
        const pam_message* message = messages[i];
        const std::string text = message->msg != nullptr ? message->msg : "";
        switch (message->msg_style) {
        case PAM_PROMPT_ECHO_OFF:
        case PAM_PROMPT_ECHO_ON: {
            std::string answer;
            std::unique_lock lock(shared->mutex);
            if (!shared->password_used && !shared->password.empty()) {
                answer = shared->password;
                shared->password_used = true;
            } else {
                shared->has_response = false;
                post(*shared, {Event::Kind::ResponseRequired, text, false});
                shared->cv.wait(lock,
                                [&] { return shared->has_response || shared->abandoned; });
                if (shared->abandoned)
                    return fail();
                answer = shared->response;
                shared->response.clear();
                shared->has_response = false;
            }
            replies[i].resp = strdup(answer.c_str());
            replies[i].resp_retcode = 0;
            break;
        }
        case PAM_ERROR_MSG: {
            std::lock_guard lock(shared->mutex);
            post(*shared, {Event::Kind::Message, text, true});
            break;
        }
        case PAM_TEXT_INFO: {
            std::lock_guard lock(shared->mutex);
            post(*shared, {Event::Kind::Message, text, false});
            break;
        }
        default:
            return fail();
        }
    }
    *responses = replies;
    return PAM_SUCCESS;
}

void PamAuth::run(std::shared_ptr<Shared> shared, std::string service, std::string user) {
    pam_conv conv{&PamAuth::conversation, shared.get()};
    pam_handle_t* handle = nullptr;
    int ret = pam_start(service.c_str(), user.c_str(), &conv, &handle);
    std::string message;
    if (ret == PAM_SUCCESS) {
        ret = pam_authenticate(handle, 0);
        if (ret == PAM_SUCCESS)
            ret = pam_acct_mgmt(handle, 0);
        message = pam_strerror(handle, ret);
        pam_end(handle, ret);
    } else {
        message = pam_strerror(nullptr, ret);
    }
    std::lock_guard lock(shared->mutex);
    shared->password.clear();
    post(*shared, {Event::Kind::Completed, message, ret == PAM_SUCCESS});
}

// Main thread: replay the worker's events as signals.
void PamAuth::drain_events() {
    std::deque<Event> events;
    {
        std::lock_guard lock(shared_->mutex);
        events.swap(shared_->events);
    }
    for (const auto& event : events) {
        switch (event.kind) {
        case Event::Kind::Message:
            message_.emit(event.text, event.flag);
            break;
        case Event::Kind::ResponseRequired:
            waiting_ = true;
            response_required_.emit();
            break;
        case Event::Kind::Completed:
            active_ = false;
            waiting_ = false;
            completed_.emit(event.flag, event.text);
            break;
        }
    }
}

} // namespace hyprshell
