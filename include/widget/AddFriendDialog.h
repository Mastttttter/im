#pragma once
#include <qtmetamacros.h>
#include <QDialog>
class QLineEdit;
class QTextEdit;

class AddFriendDialog : public QDialog {
  Q_OBJECT
  public:
  explicit AddFriendDialog(QString const &userName, QWidget *parent = nullptr);
  QString ToxIdHex() const;
  QString RequestMessage() const;

  private:
  QLineEdit *toxIdEdit_{nullptr};
  QTextEdit *messageEdit_{nullptr};
};
