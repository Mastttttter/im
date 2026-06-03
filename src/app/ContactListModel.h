#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

struct ContactItem {
  QString identifier;
  QString displayName;
  QString subtitle;
  int onlineState{0};
  int unreadCount{0};
  QString avatarText;
  QString publicKey;
  bool aiAssistant{false};
};

class ContactListModel final : public QAbstractListModel {
  Q_OBJECT

  public:
  enum Role {
    IdentifierRole = Qt::UserRole + 1,
    DisplayNameRole,
    SubtitleRole,
    OnlineStateRole,
    UnreadCountRole,
    AvatarTextRole,
    PublicKeyRole,
    AiAssistantRole
  };
  Q_ENUM(Role)

  explicit ContactListModel(QObject *parent = nullptr);

  int rowCount(QModelIndex const &parent = QModelIndex()) const override;
  QVariant data(QModelIndex const &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  Q_INVOKABLE int count() const;
  Q_INVOKABLE QString displayName(QString const &identifier) const;

  void setContacts(QVector<ContactItem> contacts);
  void upsertContact(ContactItem contact);
  void removeContact(QString const &identifier);
  void clear();
  void setUnreadCount(QString const &identifier, int unreadCount);
  bool contact(QString const &identifier, ContactItem &out) const;

  private:
  int indexOf(QString const &identifier) const;

  QVector<ContactItem> contacts_;
};
