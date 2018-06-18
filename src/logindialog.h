#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>

namespace Ui {
    class LoginDialog;
}

class LoginDialog : public QDialog {
    Q_OBJECT
public:
    LoginDialog(QWidget *parent = nullptr, QString user = QString());
    ~LoginDialog();
	QString user();
	QString password();

protected:
    void changeEvent(QEvent *e);

private:
    Ui::LoginDialog *ui;

private slots:
	void userChanged(QString);
};

#endif // LOGINDIALOG_H
