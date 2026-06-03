#pragma once
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>

namespace AppLog {
class LogHub final : public QObject {
  Q_OBJECT
  public:
  static LogHub &Instance();
  void AppendQt(QtMsgType type, QString const &category,
                QString const &message);
  void AppendInfo(QString const &category, QString const &message);
  void AppendWarn(QString const &category, QString const &message);
  void AppendError(QString const &category, QString const &message);
  QStringList Snapshot() const;
  signals:
  void NewLine(QString const &formattedLine, int qtMsgType);

  private:
  LogHub();
  QString FormatLine_(QtMsgType type, QString const &category,
                      QString const &message) const;
  void AppendLocked_(QString const &formattedLine, QtMsgType type);
  mutable QMutex *mu_{nullptr};
  QStringList lines_;
  int maxLines_{2000};
};

void Install();
}  // namespace AppLog
