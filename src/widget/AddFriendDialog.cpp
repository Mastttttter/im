#include "widget/AddFriendDialog.h"
#include <qobject.h>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

AddFriendDialog::AddFriendDialog(QString const &userName, QWidget *parent)
    : QDialog(parent) {
  setWindowTitle(QStringLiteral("add Friend"));
  setModal(true);
  toxIdEdit_ = new QLineEdit(this);
  messageEdit_ = new QTextEdit(this);
  messageEdit_->setPlaceholderText(
      QStringLiteral("(optional) friend request message"));
  messageEdit_->setFixedHeight(80);
  messageEdit_->setPlainText(QStringLiteral("hello, i'am %1").arg(userName));
  auto form = new QFormLayout();
  form->addRow(QStringLiteral("toxid: "), toxIdEdit_);
  form->addRow(QStringLiteral("message: "), messageEdit_);
  auto btns = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  btns->button(QDialogButtonBox::Ok)->setText(QStringLiteral("confirm"));
  btns->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("cancel"));
  connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
  auto root = new QVBoxLayout(this);
  root->addLayout(form);
  root->addWidget(btns);
  setLayout(root);
}

QString AddFriendDialog::ToxIdHex() const {
  return toxIdEdit_->text().trimmed();
}

QString AddFriendDialog::RequestMessage() const {
  return messageEdit_->toPlainText();
}
