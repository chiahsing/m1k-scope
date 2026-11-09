#include <QApplication>
#include <QIcon>
#include <QMessageBox>

#include "core/device.h"
#include "ui/main_window.h"

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("m1k_scope");
  QApplication::setOrganizationName("m1k_scope");
  QApplication::setWindowIcon(QIcon(":/m1k_scope.svg"));

  // Instantiate the core business logic.
  m1k_scope::core::Device device;
  if (!device.Open()) {
    QMessageBox::critical(nullptr, "M1K Scope",
                          "No M1K device found. Connect a device and try "
                          "again.");
    return 1;
  }

  // Instantiate the Qt6 user interface, passing the core controller for event
  // binding.
  m1k_scope::ui::MainWindow main_window(device);
  main_window.show();

  return app.exec();
}
