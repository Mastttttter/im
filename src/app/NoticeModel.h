#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

struct NoticeItem {
  QString timestamp;
  QString category;
  QString text;
  QString severity{"info"};
};

class NoticeModel final : public QAbstractListModel {
  Q_OBJECT

  public:
  enum Role {
    TimestampRole = Qt::UserRole + 1,
    CategoryRole,
    TextRole,
    SeverityRole
  };
  Q_ENUM(Role)

  explicit NoticeModel(QObject *parent = nullptr);

  int rowCount(QModelIndex const &parent = QModelIndex()) const override;
  QVariant data(QModelIndex const &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  Q_INVOKABLE int count() const;

  void addNotice(NoticeItem notice);
  void clear();

  private:
  QVector<NoticeItem> notices_;
};
