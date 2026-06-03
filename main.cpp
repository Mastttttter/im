#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include "app/AppController.h"
#include "core/AppLog.h"

using namespace Qt::StringLiterals;

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  AppLog::Install();
  QCoreApplication::setOrganizationName(QStringLiteral("im"));
  QCoreApplication::setApplicationName(QStringLiteral("im-qml"));
  QQuickStyle::setStyle(QStringLiteral("Material"));

  AppController controller;

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("appController"),
                                           &controller);
  engine.rootContext()->setContextProperty(QStringLiteral("friendModel"),
                                           controller.friendModel());
  engine.rootContext()->setContextProperty(QStringLiteral("groupModel"),
                                           controller.groupModel());
  engine.rootContext()->setContextProperty(QStringLiteral("chatModel"),
                                           controller.chatModel());
  engine.rootContext()->setContextProperty(QStringLiteral("noticeModel"),
                                           controller.noticeModel());

  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      []() { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);

  engine.loadFromModule(u"im"_s, u"Main"_s);
  return QGuiApplication::exec();
}
