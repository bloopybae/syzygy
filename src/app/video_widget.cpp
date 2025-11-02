#include "app/video_widget.hpp"

#include <cairomm/context.h>
#include <gdkmm/general.h>
#include <gdkmm/rgba.h>
#include <gtkmm/snapshot.h>
#include <gtkmm/stylecontext.h>
#include <pangomm/layout.h>
#include <graphene.h>

#include <algorithm>

namespace syzygy::app {

VideoWidget::VideoWidget() {
  set_hexpand(true);
  set_vexpand(true);
}

void VideoWidget::show_placeholder(const Glib::ustring& message) {
  placeholder_message_ = message;
  texture_.reset();
  frame_data_.clear();
  frame_width_ = frame_height_ = frame_stride_ = 0;
  queue_resize();
  queue_draw();
}

void VideoWidget::update_frame(const capture::Frame& frame) {
  placeholder_message_.clear();
  update_texture(frame);
  queue_draw();
}

void VideoWidget::update_texture(const capture::Frame& frame) {
  frame_width_ = frame.width;
  frame_height_ = frame.height;
  frame_stride_ = frame.stride;
  frame_data_ = frame.rgb;
  preferred_width_ = static_cast<int>(frame_width_);
  preferred_height_ = static_cast<int>(frame_height_);

  auto bytes =
      Glib::Bytes::create(frame_data_.data(), frame_data_.size());
  texture_ = Gdk::MemoryTexture::create(
      frame_width_, frame_height_, Gdk::MemoryTexture::Format::R8G8B8, bytes,
      frame_stride_);
}

void VideoWidget::snapshot_vfunc(const Glib::RefPtr<Gtk::Snapshot>& snapshot) {
  const auto allocation = get_allocation();
  const double width = static_cast<double>(allocation.get_width());
  const double height = static_cast<double>(allocation.get_height());
  auto style = get_style_context();
  graphene_rect_t rect;
  graphene_rect_init(&rect, 0.0f, 0.0f,
                     static_cast<float>(width),
                     static_cast<float>(height));
  auto cr = snapshot->append_cairo(&rect);
  if (style) {
    style->render_background(cr, 0.0, 0.0, width, height);
    style->render_frame(cr, 0.0, 0.0, width, height);
  } else {
    cr->set_source_rgb(0.0, 0.0, 0.0);
    cr->paint();
  }

  if (texture_) {
    const double tex_w = static_cast<double>(texture_->get_width());
    const double tex_h = static_cast<double>(texture_->get_height());
    const double scale =
        std::min(width / tex_w, height / tex_h);
    const double draw_w = tex_w * scale;
    const double draw_h = tex_h * scale;
    const double offset_x = (width - draw_w) / 2.0;
    const double offset_y = (height - draw_h) / 2.0;
    graphene_rect_t tex_rect;
    graphene_rect_init(&tex_rect, static_cast<float>(offset_x),
                       static_cast<float>(offset_y),
                       static_cast<float>(draw_w),
                       static_cast<float>(draw_h));
    snapshot->append_texture(texture_, &tex_rect);
    return;
  }

  if (style) {
    const auto fg = style->get_color();
    cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(),
                        fg.get_alpha());
  } else {
    cr->set_source_rgb(0.7, 0.7, 0.7);
  }
  auto layout = create_pango_layout(
      placeholder_message_.empty() ? Glib::ustring("No signal")
                                   : placeholder_message_);
  layout->set_alignment(Pango::Alignment::CENTER);
  int text_w = 0;
  int text_h = 0;
  layout->get_pixel_size(text_w, text_h);
  const double x = (width - text_w) / 2.0;
  const double y = (height - text_h) / 2.0;
  cr->move_to(x, y);
  layout->show_in_cairo_context(cr);
}

void VideoWidget::measure_vfunc(Gtk::Orientation orientation, int,
                                int& minimum, int& natural,
                                int& minimum_baseline,
                                int& natural_baseline) const {
  minimum_baseline = -1;
  natural_baseline = -1;
  const int pref =
      (orientation == Gtk::Orientation::HORIZONTAL) ? preferred_width_
                                                    : preferred_height_;
  const int fallback = (orientation == Gtk::Orientation::HORIZONTAL) ? 240 : 135;
  const int effective = pref > 0 ? pref : fallback;
  minimum = std::max(1, effective / 4);
  natural = effective;
}

}  // namespace syzygy::app
