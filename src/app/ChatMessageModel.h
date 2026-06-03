#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

struct ChatMessageItem {
  QString identifier;
  QString sender;
  QString text;
  QString timestamp;
  bool outgoing{false};
  bool system{false};
  QString messageType{"text"};
  int progress{-1};
  QString deliveryState{"sent"};
};

class ChatMessageModel final : public QAbstractListModel {
  Q_OBJECT

  public:
  enum Role {
    IdentifierRole = Qt::UserRole + 1,
    SenderRole,
    TextRole,
    TimestampRole,
    OutgoingRole,
    SystemRole,
    MessageTypeRole,
    ProgressRole,
    DeliveryStateRole
  };
  Q_ENUM(Role)

  explicit ChatMessageModel(QObject *parent = nullptr);

  int rowCount(QModelIndex const &parent = QModelIndex()) const override;
  QVariant data(QModelIndex const &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  Q_INVOKABLE int count() const;

  void setMessages(QVector<ChatMessageItem> messages);
  void appendMessage(ChatMessageItem message);
  QVector<ChatMessageItem> messages() const;
  void clear();

  private:
  QVector<ChatMessageItem> messages_;
};
