#include "bar/busy_indicator.hpp"

#include <cmath>

namespace hyprshell {

namespace {
constexpr gint64 kDurationUs = 900 * 1000; // Style.animationSlow (450ms) * 2
}

BusyIndicator::BusyIndicator(int size, double stroke) : stroke_(stroke) {
    set_size_request(size, size);
    set_halign(Gtk::Align::CENTER);
    set_valign(Gtk::Align::CENTER);
    set_draw_func(sigc::mem_fun(*this, &BusyIndicator::draw));
    signal_map().connect(sigc::mem_fun(*this, &BusyIndicator::update_tick));
    signal_unmap().connect(sigc::mem_fun(*this, &BusyIndicator::update_tick));
}

void BusyIndicator::set_running(bool running) {
    if (running_ == running)
        return;
    running_ = running;
    update_tick();
    queue_draw();
}

// one tick callback while mapped && running; removed otherwise
void BusyIndicator::update_tick() {
    const bool want = running_ && get_mapped();
    if (want && tick_id_ == 0) {
        tick_id_ = add_tick_callback([this](const Glib::RefPtr<Gdk::FrameClock>&) {
            queue_draw();
            return true;
        });
    } else if (!want && tick_id_ != 0) {
        remove_tick_callback(tick_id_);
        tick_id_ = 0;
    }
}

void BusyIndicator::draw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
    if (!running_)
        return;
    const double cx = width / 2.0;
    const double cy = height / 2.0;
    const double radius = std::min(width, height) / 2.0 - stroke_ / 2.0;
    double rotation = 0.0;
    if (auto clock = get_frame_clock()) {
        const gint64 now = clock->get_frame_time();
        rotation = 2.0 * M_PI * static_cast<double>(now % kDurationUs) / kDurationUs;
    }
    const auto color = get_color();
    cr->set_source_rgba(color.get_red(), color.get_green(), color.get_blue(),
                        color.get_alpha());
    cr->set_line_width(stroke_);
    cr->set_line_cap(Cairo::Context::LineCap::ROUND);
    const double start = -M_PI / 2.0 + rotation;
    cr->arc(cx, cy, radius, start, start + M_PI * 1.5);
    cr->stroke();
}

} // namespace hyprshell
