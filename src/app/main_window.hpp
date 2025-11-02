#pragma once

// Copyright (c) 2025 Zoe Gates <zoe@zeocities.dev>

#include "app/video_widget.hpp"
#include "audio/pipewire_controller.hpp"
#include "capture/capture_device.hpp"
#include "capture/capture_session.hpp"
#include "capture/device_monitor.hpp"
#include "settings/settings_manager.hpp"

#include <gtkmm/applicationwindow.h>
#include <gtkmm/box.h>
#include <gtkmm/centerbox.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/label.h>
#include <gtkmm/levelbar.h>
#include <gtkmm/scale.h>
#include <gtkmm/window.h>

#include "syzygy/clock.hpp"

#include <optional>

namespace syzygy::app {

class MainWindow : public Gtk::ApplicationWindow {
 public:
  MainWindow();
  ~MainWindow() override;

 private:
  void build_ui();
  void refresh_device_list(bool restart_stream);
  void start_current_device();
  bool on_frame_tick(const Glib::RefPtr<Gdk::FrameClock>& clock);
  void update_capture_stats(const capture::Frame& frame);
  void update_fullscreen_ui();
  void set_fullscreen_state(bool enable);
  bool on_key_pressed(guint keyval, guint keycode, Gdk::ModifierType state);
  void reset_video_timeline();

  void on_device_changed();
  void on_volume_changed();

  Gtk::Box root_{Gtk::Orientation::VERTICAL};
  Gtk::Box control_bar_{Gtk::Orientation::HORIZONTAL};
  Gtk::ComboBoxText device_combo_;
  Gtk::Scale volume_scale_;
  Gtk::LevelBar audio_level_bar_;
  Gtk::Label audio_status_label_;
  Gtk::Label capture_stats_label_;
  VideoWidget video_widget_;
  Gtk::HeaderBar* header_bar_{nullptr};
  Gtk::Label* title_label_{nullptr};
  Gtk::CenterBox status_bar_;
  Gtk::Box status_left_{Gtk::Orientation::HORIZONTAL};
  Gtk::Box status_center_{Gtk::Orientation::HORIZONTAL};
  Gtk::Box status_right_{Gtk::Orientation::HORIZONTAL};

  settings::SettingsManager settings_;
  capture::CaptureSession capture_session_;
  audio::PipeWireController audio_controller_;
  std::unique_ptr<capture::DeviceMonitor> device_monitor_;
  std::vector<capture::CaptureDevice> devices_;
  bool suppress_device_callback_{false};
  bool fullscreen_{false};
  Glib::RefPtr<Gtk::EventControllerKey> key_controller_;
  std::optional<syzygy::clock::TimePoint> video_base_time_;
  std::optional<syzygy::clock::TimePoint> last_frame_time_;
  double current_fps_{0.0};
  double audio_level_smooth_{0.0};
  bool audio_using_fallback_{false};
  double monitor_interval_ms_{16.0};
};

}  // namespace syzygy::app
