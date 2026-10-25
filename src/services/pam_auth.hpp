#pragma once

#include <glibmm.h>
#include <sigc++/sigc++.h>

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

struct pam_message;
struct pam_response;

namespace hyprshell {

// PAM authentication for the lock screen — the role Quickshell's PamContext
// plays for Noctalia's LockContext. pam_authenticate() is synchronous and
// pam_unix sleeps on a wrong password, so each attempt runs on a worker
// thread (the one place the project spawns a thread); the PAM conversation
// hands its prompts back to the main loop through a Glib::Dispatcher and
// blocks on a condition variable until respond() supplies the answer.
//
// Semantics follow PamContext: start() begins a transaction; a password
// prompt is answered immediately with the password handed to start() when it
// is non-empty, otherwise signal_response_required() fires and the prompt
// waits for respond(). Info/error messages and the final result are emitted
// on the main thread.
class PamAuth {
public:
    PamAuth();
    ~PamAuth();

    PamAuth(const PamAuth&) = delete;
    PamAuth& operator=(const PamAuth&) = delete;

    // Detected PAM service: $HS_PAM_SERVICE, else the first of login /
    // system-auth / common-auth that exists in /etc/pam.d (Noctalia's probe).
    static std::string detect_service();

    bool active() const { return active_; }
    bool waiting_for_response() const { return waiting_; }

    // Begins a transaction for `user`; `password` answers the first prompt
    // when non-empty. Ignored while a transaction is active.
    void start(const std::string& service, const std::string& user,
               const std::string& password);
    // Answers a pending prompt (after signal_response_required).
    void respond(const std::string& password);

    // PAM_TEXT_INFO / PAM_ERROR_MSG text from the modules
    sigc::signal<void(const std::string&, bool /*is_error*/)>& signal_message() {
        return message_;
    }
    // a password prompt is waiting for respond()
    sigc::signal<void()>& signal_response_required() { return response_required_; }
    // transaction finished: success, or failure with PAM's message
    sigc::signal<void(bool, const std::string&)>& signal_completed() { return completed_; }

private:
    struct Event {
        enum class Kind { Message, ResponseRequired, Completed } kind;
        std::string text;
        bool flag = false; // Message: is_error; Completed: success
    };

    // Everything the worker thread touches; outlives the thread via shared_ptr.
    struct Shared {
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<Event> events;
        std::string password;    // the initial password for the first prompt
        bool password_used = false;
        std::string response;    // respond() → the waiting conversation
        bool has_response = false;
        bool abandoned = false;  // PamAuth destroyed: prompts fail instead of blocking
        Glib::Dispatcher* dispatcher = nullptr;
    };

    static int conversation(int count, const ::pam_message** messages,
                            ::pam_response** responses, void* data);
    static void post(Shared& shared, Event event); // caller holds shared.mutex
    static void run(std::shared_ptr<Shared> shared, std::string service, std::string user);
    void drain_events();

    std::shared_ptr<Shared> shared_;
    std::thread thread_;
    Glib::Dispatcher dispatcher_;
    bool active_ = false;
    bool waiting_ = false;
    sigc::signal<void(const std::string&, bool)> message_;
    sigc::signal<void()> response_required_;
    sigc::signal<void(bool, const std::string&)> completed_;
};

} // namespace hyprshell
