#include "app/ContactListModel.h"

#include <utility>

ContactListModel::ContactListModel(QObject *parent) : QAbstractListModel(parent) {}

int ContactListModel::rowCount(QModelIndex const &parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return contacts_.size();
}

QVariant ContactListModel::data(QModelIndex const &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= contacts_.size()) {
    return {};
  }

  ContactItem const &contact = contacts_.at(index.row());
  switch (role) {
  case IdentifierRole: return contact.identifier;
  case DisplayNameRole: return contact.displayName;
  case SubtitleRole: return contact.subtitle;
  case OnlineStateRole: return contact.onlineState;
  case UnreadCountRole: return contact.unreadCount;
  case AvatarTextRole: return contact.avatarText;
  case PublicKeyRole: return contact.publicKey;
  case AiAssistantRole: return contact.aiAssistant;
  default: return {};
  }
}

QHash<int, QByteArray> ContactListModel::roleNames() const {
  return {{IdentifierRole, "identifier"},
          {DisplayNameRole, "displayName"},
          {SubtitleRole, "subtitle"},
          {OnlineStateRole, "onlineState"},
          {UnreadCountRole, "unreadCount"},
          {AvatarTextRole, "avatarText"},
          {PublicKeyRole, "publicKey"},
          {AiAssistantRole, "aiAssistant"}};
}

int ContactListModel::count() const { return contacts_.size(); }

QString ContactListModel::displayName(QString const &identifier) const {
  int const row = indexOf(identifier);
  if (row < 0) {
    return {};
  }
  return contacts_.at(row).displayName;
}

void ContactListModel::setContacts(QVector<ContactItem> contacts) {
  beginResetModel();
  contacts_ = std::move(contacts);
  endResetModel();
}

void ContactListModel::upsertContact(ContactItem contact) {
  int const row = indexOf(contact.identifier);
  if (row < 0) {
    beginInsertRows(QModelIndex(), contacts_.size(), contacts_.size());
    contacts_.push_back(std::move(contact));
    endInsertRows();
    return;
  }

  contacts_[row] = std::move(contact);
  QModelIndex const changed = index(row, 0);
  emit dataChanged(changed, changed);
}

void ContactListModel::removeContact(QString const &identifier) {
  int const row = indexOf(identifier);
  if (row < 0) {
    return;
  }
  beginRemoveRows(QModelIndex(), row, row);
  contacts_.removeAt(row);
  endRemoveRows();
}

void ContactListModel::clear() {
  if (contacts_.isEmpty()) {
    return;
  }
  beginResetModel();
  contacts_.clear();
  endResetModel();
}

void ContactListModel::setUnreadCount(QString const &identifier, int unreadCount) {
  int const row = indexOf(identifier);
  if (row < 0) {
    return;
  }
  contacts_[row].unreadCount = unreadCount;
  QModelIndex const changed = index(row, 0);
  emit dataChanged(changed, changed, {UnreadCountRole});
}

bool ContactListModel::contact(QString const &identifier, ContactItem &out) const {
  int const row = indexOf(identifier);
  if (row < 0) {
    return false;
  }
  out = contacts_.at(row);
  return true;
}

int ContactListModel::indexOf(QString const &identifier) const {
  for (int i = 0; i < contacts_.size(); ++i) {
    if (contacts_.at(i).identifier == identifier) {
      return i;
    }
  }
  return -1;
}
