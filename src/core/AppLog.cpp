#include "AppLog.h"
#include <atomic>
#include <cstdlib>
#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>
#include <QtGlobal>
#include <QThread>

namespace AppLog {
using namespace Qt::StringLiterals;

QString TypeToText_(QtMsgType type) {
  switch (type) {
  case QtDebugMsg:    return QStringLiteral("DEBUG");
  case QtInfoMsg:     return QStringLiteral("INFO");
  case QtWarningMsg:  return QStringLiteral("WARN");
  case QtCriticalMsg: return QStringLiteral("ERROR");
  case QtFatalMsg:    return QStringLiteral("FATAL");
  default:            return QStringLiteral("LOG");
  }
}

LogHub::LogHub() : QObject(nullptr), mu_(new QMutex()) {}

LogHub &LogHub::Instance() {
  static LogHub inst;
  return inst;
}

QString LogHub::FormatLine_(QtMsgType type, QString const &category,
                            QString const &message) const {
  QString const ts = QDateTime::currentDateTime().toString(u"HH:mm:ss.zzz"_s);
  QString const level = TypeToText_(type);
  QString const cat =
      category.trimmed().isEmpty() ? u"app"_s : category.trimmed();
  QString const msg = message.trimmed();
  return u"[%1][%2][%3] %4"_s.arg(ts, level, cat, msg);
}

void LogHub::AppendLocked_(QString const &formattedLine, QtMsgType type) {
  lines_.push_back(formattedLine);
  if (lines_.size() > maxLines_) {
    int const drop = lines_.size() - maxLines_;
    lines_.erase(lines_.begin(), lines_.begin() + drop);
  }
  emit NewLine(formattedLine, static_cast<int>(type));
}

void LogHub::AppendQt(QtMsgType type, QString const &category,
                      QString const &message) {
  QMutexLocker locker(static_cast<QMutex *>(mu_));
  AppendLocked_(FormatLine_(type, category, message), type);
}

void LogHub::AppendInfo(QString const &category, QString const &message) {
  AppendQt(QtInfoMsg, category, message);
}

void LogHub::AppendWarn(QString const &category, QString const &message) {
  AppendQt(QtWarningMsg, category, message);
}

void LogHub::AppendError(QString const &category, QString const &message) {
  AppendQt(QtCriticalMsg, category, message);
}

QStringList LogHub::Snapshot() const {
  QMutexLocker locker(static_cast<QMutex *>(mu_));
  return lines_;
}

static void QtMessageHandler_(QtMsgType type, QMessageLogContext const &ctx,
                              QString const &msg) {
  QString cat;
  if (ctx.category && ctx.category[0] != '\0') {
    cat = QString::fromUtf8(ctx.category);
  } else {
    cat = u"qt"_s;
  }
  LogHub::Instance().AppendQt(type, cat, msg);
  if (type == QtFatalMsg) {
    std::abort();
  }
}

static std::atomic<bool> g_installed{false};

void Install() {
  bool expected = false;
  if (!g_installed.compare_exchange_strong(expected, true)) {
    return;
  }
  qInstallMessageHandler(QtMessageHandler_);
  LogHub::Instance().AppendInfo(u"app"_s, u"AppLog installed"_s);
}

}  // namespace AppLog
