#include "app/main_window.hpp"

#include "syzygy/log.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <gdk/gdkkeysyms.h>
#include <gdkmm/display.h>
#include <gdkmm/monitor.h>
#include <giomm/listmodel.h>
#include <glibmm/main.h>
#include <gtkmm/separator.h>
#include <iomanip>
#include <memory>
#include <sstream>
#include <sigc++/sigc++.h>

namespace syzygy::app {

MainWindow::MainWindow()
    : Gtk::ApplicationWindow(),
      capture_session_() {
  set_title("Syzygy Preview");
  set_default_size(1280, 720);

  header_bar_ = Gtk::make_managed<Gtk::HeaderBar>();
  title_label_ = Gtk::make_managed<Gtk::Label>("Syzygy");
  header_bar_->set_title_widget(*title_label_);
  header_bar_->set_show_title_buttons(true);
  set_titlebar(*header_bar_);

  key_controller_ = Gtk::EventControllerKey::create();
  key_controller_->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
  key_controller_->signal_key_pressed().connect(
      sigc::mem_fun(*this, &MainWindow::on_key_pressed), false);
  add_controller(key_controller_);

  build_ui();
  volume_scale_.set_value(settings_.data().audio_gain);
  audio_controller_.set_gain(static_cast<float>(settings_.data().audio_gain));

  update_fullscreen_ui();
  refresh_device_list(true);
  audio_status_label_.set_text("Audio: idle");
  capture_stats_label_.set_text("Resolution: —");
  audio_level_bar_.set_value(0.0);

  add_tick_callback(sigc::mem_fun(*this, &MainWindow::on_frame_tick));

  device_monitor_ = std::make_unique<capture::DeviceMonitor>([this]() {
    Glib::signal_idle().connect_once(
        sigc::bind(sigc::mem_fun(*this, &MainWindow::refresh_device_list), false));
  });
}

MainWindow::~MainWindow() {
  capture_session_.stop();
  audio_controller_.stop();
}

void MainWindow::build_ui() {
  root_.set_spacing(12);
  root_.set_margin(12);
  set_child(root_);

  control_bar_.set_spacing(12);
  control_bar_.set_hexpand(true);
  control_bar_.set_valign(Gtk::Align::START);

  auto* device_column =
      Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
  auto* device_label = Gtk::make_managed<Gtk::Label>("Capture device");
  device_label->set_halign(Gtk::Align::START);
  device_label->add_css_class("dim-label");
  device_column->append(*device_label);
  device_combo_.set_hexpand(true);
  device_column->append(device_combo_);

  control_bar_.append(*device_column);
  root_.append(control_bar_);

  video_widget_.set_hexpand(true);
  video_widget_.set_vexpand(true);
  video_widget_.show_placeholder("Awaiting capture frame...");
  root_.append(video_widget_);

  auto* separator = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
  separator->set_margin_top(8);
  root_.append(*separator);

  status_left_.set_spacing(8);
  status_left_.set_hexpand(true);
  status_left_.set_valign(Gtk::Align::CENTER);
  auto* volume_label = Gtk::make_managed<Gtk::Label>("Volume");
  volume_label->set_halign(Gtk::Align::START);
  volume_label->add_css_class("dim-label");
  status_left_.append(*volume_label);
  volume_scale_.set_range(0.0, 2.0);
  volume_scale_.set_draw_value(false);
  volume_scale_.set_hexpand(true);
  status_left_.append(volume_scale_);

  audio_level_bar_.set_min_value(0.0);
  audio_level_bar_.set_max_value(1.0);
  audio_level_bar_.set_value(0.0);
  audio_level_bar_.set_size_request(120, -1);
  audio_level_bar_.set_valign(Gtk::Align::CENTER);
  status_left_.append(audio_level_bar_);

  status_center_.set_spacing(6);
  status_center_.set_valign(Gtk::Align::CENTER);
  status_center_.set_hexpand(true);
  audio_status_label_.set_halign(Gtk::Align::CENTER);
  status_center_.append(audio_status_label_);

  status_right_.set_spacing(6);
  status_right_.set_valign(Gtk::Align::CENTER);
  capture_stats_label_.set_halign(Gtk::Align::END);
  capture_stats_label_.set_hexpand(true);
  status_right_.append(capture_stats_label_);

  status_bar_.set_hexpand(true);
  status_bar_.set_margin_top(8);
  status_bar_.set_start_widget(status_left_);
  status_bar_.set_center_widget(status_center_);
  status_bar_.set_end_widget(status_right_);
  root_.append(status_bar_);

  device_combo_.signal_changed().connect(
      sigc::mem_fun(*this, &MainWindow::on_device_changed));
  volume_scale_.signal_value_changed().connect(
      sigc::mem_fun(*this, &MainWindow::on_volume_changed));
}

void MainWindow::refresh_device_list(bool restart_stream) {
  devices_ = capture::enumerate_devices();
  const auto previous_id = device_combo_.get_active_id();

  suppress_device_callback_ = true;
  device_combo_.remove_all();
  for (const auto& device : devices_) {
    std::string label = device.name.empty() ? device.path : device.name;
    label += " (" + device.path + ")";
    device_combo_.append(device.path, label);
  }

  std::string desired = settings_.data().last_video_device;
  if (!previous_id.empty()) {
    desired = previous_id;
  }

  if (!desired.empty()) {
    device_combo_.set_active_id(desired);
  } else if (!devices_.empty()) {
    device_combo_.set_active(0);
  }
  suppress_device_callback_ = false;
  if (restart_stream) {
    start_current_device();
  }
}

void MainWindow::start_current_device() {
  if (suppress_device_callback_) {
    return;
  }

  const auto id = device_combo_.get_active_id();
  if (id.empty()) {
    capture_session_.stop();
    audio_controller_.stop();
    video_widget_.show_placeholder("Select a capture device");
    reset_video_timeline();
    audio_status_label_.set_text("Audio: idle");
    capture_stats_label_.set_text("Resolution: —");
    audio_level_smooth_ = 0.0;
    audio_level_bar_.set_value(0.0);
    audio_using_fallback_ = false;
    if (title_label_) {
      title_label_->set_text("Syzygy");
    }
    return;
  }

  audio_controller_.stop();
  reset_video_timeline();

  syzygy::log::info("Switching capture device", id);
  constexpr auto preset = capture::LatencyPreset::UltraLow;
  if (!capture_session_.start(id, preset)) {
    video_widget_.show_placeholder("Unable to start capture");
    capture_stats_label_.set_text("Capture unavailable");
    audio_status_label_.set_text("Audio: idle");
    audio_level_smooth_ = 0.0;
    audio_level_bar_.set_value(0.0);
    audio_using_fallback_ = false;
    if (title_label_) {
      title_label_->set_text("Syzygy");
    }
    return;
  }

  settings_.set_last_video_device(id);
  if (title_label_) {
    std::string heading = "Syzygy";
    const auto active = device_combo_.get_active_text();
    if (!active.empty()) {
      heading += " — " + active;
    }
    title_label_->set_text(heading);
  }
  capture_stats_label_.set_text("Awaiting frames...");
  audio_status_label_.set_text("Audio: connecting...");
  std::optional<std::string> bus_path;
  std::optional<std::string> label;
  const auto it = std::find_if(devices_.begin(), devices_.end(),
                               [&](const capture::CaptureDevice& device) {
                                 return device.path == id;
                               });
  if (it != devices_.end()) {
    if (!it->bus.empty()) {
      bus_path = it->bus;
    }
    if (!it->name.empty()) {
      label = it->name;
    }
  }

  bool used_fallback = false;
  bool audio_started = audio_controller_.start(std::nullopt, bus_path, label);
  if (!audio_started) {
    used_fallback = true;
    syzygy::log::warn("Unable to match capture audio node; falling back to default route");
    audio_started = audio_controller_.start();
  }
  if (!audio_started) {
    syzygy::log::warn("Unable to start PipeWire capture stream");
    audio_status_label_.set_text("Audio: unavailable");
    audio_level_smooth_ = 0.0;
    audio_level_bar_.set_value(0.0);
    audio_using_fallback_ = false;
    return;
  }
  audio_using_fallback_ = used_fallback;
  const uint32_t active_rate = audio_controller_.sample_rate();
  const uint32_t active_channels = audio_controller_.channels();
  std::ostringstream status;
  status.setf(std::ios::fixed);
  status << (used_fallback ? "Audio: default route" : "Audio: capture source")
         << " (" << active_channels << "ch @ "
         << std::setprecision(1) << (static_cast<double>(active_rate) / 1000.0)
         << " kHz)";
  audio_status_label_.set_text(status.str());
  audio_level_smooth_ = 0.0;
  audio_level_bar_.set_value(0.0);
  const double gain = volume_scale_.get_value();
  audio_controller_.set_gain(static_cast<float>(gain));
  syzygy::log::info("Audio route",
                    used_fallback ? "default" : "matched",
                    "gain", gain,
                    "bus", bus_path.value_or(""),
                    "label", label.value_or(""));
}

bool MainWindow::on_frame_tick(const Glib::RefPtr<Gdk::FrameClock>& clock) {
  (void)clock;
  if (capture_session_.is_running()) {
    if (auto frame = capture_session_.latest_frame()) {
      if (!video_base_time_) {
        video_base_time_ = frame->capture_time;
      }
      if (last_frame_time_) {
        const double delta_ms =
            std::chrono::duration<double, std::milli>(frame->capture_time - *last_frame_time_)
                .count();
        if (delta_ms > 0.0) {
          const double instant_fps = 1000.0 / delta_ms;
          if (current_fps_ <= 0.0) {
            current_fps_ = instant_fps;
          } else {
            current_fps_ = (current_fps_ * 0.85) + (instant_fps * 0.15);
          }
        }
      }
      last_frame_time_ = frame->capture_time;
      update_capture_stats(*frame);

      video_widget_.update_frame(*frame);
    }
  }
  const double peak = std::clamp(static_cast<double>(audio_controller_.peak_level()), 0.0, 1.0);
  audio_level_smooth_ = (audio_level_smooth_ * 0.85) + (peak * 0.15);
  audio_level_bar_.set_value(std::clamp(audio_level_smooth_, 0.0, 1.0));
  return true;
}

void MainWindow::update_capture_stats(const capture::Frame& frame) {
  std::ostringstream oss;
  oss.setf(std::ios::fixed);
  oss << frame.width << " x " << frame.height;
  if (current_fps_ > 0.1) {
    oss << " @ " << std::setprecision(1) << current_fps_ << " Hz";
  }
  if (audio_using_fallback_) {
    oss << " (audio fallback)";
  }
  capture_stats_label_.set_text(oss.str());
}

void MainWindow::on_device_changed() {
  if (suppress_device_callback_) {
    return;
  }
  start_current_device();
}

void MainWindow::on_volume_changed() {
  const double gain = volume_scale_.get_value();
  audio_controller_.set_gain(static_cast<float>(gain));
  settings_.set_audio_gain(gain);
}

void MainWindow::update_fullscreen_ui() {
  set_decorated(!fullscreen_);
  if (header_bar_) {
    header_bar_->set_visible(!fullscreen_);
  }
  const int margin = fullscreen_ ? 0 : 12;
  root_.set_margin(margin);
  root_.set_spacing(fullscreen_ ? 0 : 12);
  control_bar_.set_visible(!fullscreen_);
  status_bar_.set_visible(!fullscreen_);
  if (fullscreen_) {
    video_widget_.set_hexpand(true);
    video_widget_.set_vexpand(true);
  }
}

void MainWindow::set_fullscreen_state(bool enable) {
  if (fullscreen_ == enable) {
    return;
  }
  fullscreen_ = enable;
  if (fullscreen_) {
    fullscreen();
  } else {
    unfullscreen();
  }

  auto display = Gdk::Display::get_default();
  if (display) {
    Glib::RefPtr<Gdk::Monitor> monitor;
    if (auto surface = get_surface()) {
      monitor = display->get_monitor_at_surface(surface);
    }
    if (!monitor) {
      auto monitors = display->get_monitors();
      if (monitors) {
        auto obj = monitors->get_object(0);
        if (obj) {
          monitor = std::dynamic_pointer_cast<Gdk::Monitor>(obj);
        }
      }
    }
    if (monitor) {
      const int refresh_millihz = monitor->get_refresh_rate();
      if (refresh_millihz > 0) {
        monitor_interval_ms_ =
            1000000.0 / static_cast<double>(refresh_millihz);
      }
    }
  }

  update_fullscreen_ui();
}

bool MainWindow::on_key_pressed(guint keyval, guint, Gdk::ModifierType) {
  if (keyval == GDK_KEY_F11) {
    set_fullscreen_state(!fullscreen_);
    return true;
  }
  if (keyval == GDK_KEY_Escape && fullscreen_) {
    set_fullscreen_state(false);
    return true;
  }
  return false;
}

void MainWindow::reset_video_timeline() {
  video_base_time_.reset();
  last_frame_time_.reset();
  current_fps_ = 0.0;
}

}  // namespace syzygy::app
