#include <QtGui>
#include "logindialog.h"
#include "ui_logindialog.h"
#include <QScreen>
#include <QPushButton>

LoginDialog::LoginDialog(QWidget *parent, QString user) :
    QDialog(parent),
    ui(new Ui::LoginDialog)
{
    ui->setupUi(this);
    if (!user.isNull())
    {
        connect(ui->eUser, &QLineEdit::textChanged, this, &LoginDialog::userChanged);
        ui->eUser->setText(user);
    }
    else
    {
        ui->lUser->setVisible(false);
        ui->eUser->setVisible(false);
    }
    if (!user.isEmpty())
        ui->ePass->setFocus();

    adjustSize();
    QRect frect = frameGeometry();
    frect.moveCenter(parent->frameGeometry().center());
    move(frect.topLeft());
}

LoginDialog::~LoginDialog()
{
    delete ui;
}

QString LoginDialog::user()
{
    return ui->eUser->text();
}

QString LoginDialog::password()
{
    return ui->ePass->text();
}

void LoginDialog::changeEvent(QEvent *e)
{
    QDialog::changeEvent(e);
    switch (e->type()) {
    case QEvent::LanguageChange:
        ui->retranslateUi(this);
        break;
    default:
        break;
    }
}

void LoginDialog::userChanged(QString val)
{
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(val.length() > 0);
}
