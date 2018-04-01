#ifndef CONNECTIONDIALOG_H
#define CONNECTIONDIALOG_H

#include <QDialog>

namespace Ui {
class ConnectionDialog;
}

class QAbstractButton;

class ConnectionDialog : public QDialog
{
	Q_OBJECT
	
public:
    explicit ConnectionDialog(QWidget *parent = 0, QString name = QString(), QString connectionString = QString());
    ~ConnectionDialog();
    QString name();
    QString connectionString();
	
protected:
	void changeEvent(QEvent *e);
	
private slots:
	void on_buttonBox_clicked(QAbstractButton *button);

private:
    Ui::ConnectionDialog *ui;
};

#endif // CONNECTIONDIALOG_H
