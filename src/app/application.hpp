#pragma once

// Copyright (c) 2025 Zoe Gates <zoe@zeocities.dev>

#include <gtkmm/application.h>

namespace syzygy::app {

class MainWindow;

class Application : public Gtk::Application {
 protected:
  Application();

  void on_activate() override;

 public:
  static Glib::RefPtr<Application> create();

 private:
  MainWindow* main_window_{nullptr};
};

}  // namespace syzygy::app

