#include "app/application.hpp"

#include "app/main_window.hpp"

namespace syzygy::app {

Application::Application()
    : Gtk::Application("dev.zeocities.syzygy") {}

Glib::RefPtr<Application> Application::create() {
  return Glib::make_refptr_for_instance<Application>(new Application());
}

void Application::on_activate() {
  Gtk::Application::on_activate();

  if (!main_window_) {
    main_window_ = new MainWindow();
    add_window(*main_window_);
    main_window_->set_icon_name("syzygy");
  }
  main_window_->present();
}

}  // namespace syzygy::app
