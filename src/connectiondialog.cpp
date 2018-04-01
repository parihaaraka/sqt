#include "connectiondialog.h"
#include "ui_connectiondialog.h"
#include <QSettings>
#include <QDesktopWidget>

ConnectionDialog::ConnectionDialog(QWidget *parent, QString name, QString connectionString) :
    QDialog(parent),
    ui(new Ui::ConnectionDialog)
{
    ui->setupUi(this);
    ui->editCS->setWordWrapMode(QTextOption::WrapAnywhere);
    setWindowFlags(Qt::Window | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);

    if (connectionString.isEmpty())
    {
        connectionString =
            #ifdef Q_OS_WIN32
                    "Driver={SQL Server Native Client 11.0};Server=srv_name;Database=db_name;Trusted_Connection=yes;App=sqt;"
            #else
                    "Driver=FreeTDS;Server=mssql.local;TDS_Version=7.2;Port=1433;Database=master;UID=%user%;PWD=%pass%;ClientCharset=UTF-8;App=sqt;"
            #endif
                    "\n or\n"
                    "host=127.0.0.1 port=5432 dbname=postgres user=%user% password=%pass% connect_timeout=5";
    }
    ui->editCS->setPlainText(connectionString);
    ui->editName->setText(name);
    QRect frect = frameGeometry();
    frect.moveCenter(QDesktopWidget().availableGeometry().center());
    move(frect.topLeft());
}

ConnectionDialog::~ConnectionDialog()
{
    delete ui;
}

QString ConnectionDialog::name()
{
    return ui->editName->text();
}

QString ConnectionDialog::connectionString()
{
    return ui->editCS->toPlainText();
}

void ConnectionDialog::changeEvent(QEvent *e)
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

void ConnectionDialog::on_buttonBox_clicked(QAbstractButton *button)
{
    if (ui->buttonBox->standardButton(button) == QDialogButtonBox::Ok)
        accept();
    else
        reject();
}
