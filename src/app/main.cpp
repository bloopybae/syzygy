#include "app/application.hpp"

int main(int argc, char* argv[]) {
  auto app = syzygy::app::Application::create();
  return app->run(argc, argv);
}

