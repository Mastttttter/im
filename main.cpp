#include "widget/MainWindow.h"
#include <QApplication>

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  MainWindow win(QStringLiteral("default"), QString{});
  win.show();
  return app.exec();
}
