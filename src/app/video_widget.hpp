#pragma once

// Copyright (c) 2025 Zoe Gates <zoe@zeocities.dev>

#include "capture/capture_session.hpp"

#include <gdkmm/memorytexture.h>
#include <gtkmm/snapshot.h>
#include <gtkmm/widget.h>
#include <glibmm/bytes.h>

#include <vector>

namespace syzygy::app {

class VideoWidget : public Gtk::Widget {
 public:
  VideoWidget();

  void update_frame(const capture::Frame& frame);
  void show_placeholder(const Glib::ustring& message);

 protected:
  void snapshot_vfunc(const Glib::RefPtr<Gtk::Snapshot>& snapshot) override;
  void measure_vfunc(Gtk::Orientation orientation, int for_size, int& minimum,
                     int& natural, int& minimum_baseline,
                     int& natural_baseline) const override;

 private:
  void update_texture(const capture::Frame& frame);

  Glib::RefPtr<Gdk::Texture> texture_;
  std::vector<uint8_t> frame_data_;
  uint32_t frame_width_{0};
  uint32_t frame_height_{0};
  uint32_t frame_stride_{0};
  int preferred_width_{960};
  int preferred_height_{540};
  Glib::ustring placeholder_message_{"No signal"};
};

}  // namespace syzygy::app
