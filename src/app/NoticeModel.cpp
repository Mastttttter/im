#include "app/NoticeModel.h"

#include <utility>

NoticeModel::NoticeModel(QObject *parent) : QAbstractListModel(parent) {}

int NoticeModel::rowCount(QModelIndex const &parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return notices_.size();
}

QVariant NoticeModel::data(QModelIndex const &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= notices_.size()) {
    return {};
  }

  NoticeItem const &notice = notices_.at(index.row());
  switch (role) {
  case TimestampRole: return notice.timestamp;
  case CategoryRole: return notice.category;
  case TextRole: return notice.text;
  case SeverityRole: return notice.severity;
  default: return {};
  }
}

QHash<int, QByteArray> NoticeModel::roleNames() const {
  return {{TimestampRole, "timestamp"},
          {CategoryRole, "category"},
          {TextRole, "text"},
          {SeverityRole, "severity"}};
}

int NoticeModel::count() const { return notices_.size(); }

void NoticeModel::addNotice(NoticeItem notice) {
  beginInsertRows(QModelIndex(), notices_.size(), notices_.size());
  notices_.push_back(std::move(notice));
  endInsertRows();

  constexpr qsizetype kMaxNoticeRows = 300;
  if (notices_.size() > kMaxNoticeRows) {
    beginRemoveRows(QModelIndex(), 0, 0);
    notices_.removeAt(0);
    endRemoveRows();
  }
}

void NoticeModel::clear() {
  if (notices_.isEmpty()) {
    return;
  }
  beginResetModel();
  notices_.clear();
  endResetModel();
}
