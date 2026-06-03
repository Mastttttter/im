#include "app/ChatMessageModel.h"

#include <utility>

ChatMessageModel::ChatMessageModel(QObject *parent) : QAbstractListModel(parent) {}

int ChatMessageModel::rowCount(QModelIndex const &parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return messages_.size();
}

QVariant ChatMessageModel::data(QModelIndex const &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= messages_.size()) {
    return {};
  }

  ChatMessageItem const &message = messages_.at(index.row());
  switch (role) {
  case IdentifierRole: return message.identifier;
  case SenderRole: return message.sender;
  case TextRole: return message.text;
  case TimestampRole: return message.timestamp;
  case OutgoingRole: return message.outgoing;
  case SystemRole: return message.system;
  case MessageTypeRole: return message.messageType;
  case ProgressRole: return message.progress;
  case DeliveryStateRole: return message.deliveryState;
  default: return {};
  }
}

QHash<int, QByteArray> ChatMessageModel::roleNames() const {
  return {{IdentifierRole, "identifier"},
          {SenderRole, "sender"},
          {TextRole, "text"},
          {TimestampRole, "timestamp"},
          {OutgoingRole, "outgoing"},
          {SystemRole, "system"},
          {MessageTypeRole, "messageType"},
          {ProgressRole, "progress"},
          {DeliveryStateRole, "deliveryState"}};
}

int ChatMessageModel::count() const { return messages_.size(); }

void ChatMessageModel::setMessages(QVector<ChatMessageItem> messages) {
  beginResetModel();
  messages_ = std::move(messages);
  endResetModel();
}

void ChatMessageModel::appendMessage(ChatMessageItem message) {
  beginInsertRows(QModelIndex(), messages_.size(), messages_.size());
  messages_.push_back(std::move(message));
  endInsertRows();
}

QVector<ChatMessageItem> ChatMessageModel::messages() const { return messages_; }

void ChatMessageModel::clear() {
  if (messages_.isEmpty()) {
    return;
  }
  beginResetModel();
  messages_.clear();
  endResetModel();
}
