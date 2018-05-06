#include "settingsdialog.h"
#include "ui_settingsdialog.h"
#include "settings.h"
#include <QPushButton>

SettingsDialog::SettingsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SettingsDialog)
{
    ui->setupUi(this);
    ui->appStyle->setPlainText(SqtSettings::value("appStyle").toString());
    ui->tabSize->setValue(SqtSettings::value("tabSize").toInt());
    ui->singleRowMode->setChecked(SqtSettings::value("pgSingleRowMode", false).toBool());
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

void SettingsDialog::on_okBtn_clicked()
{
    SqtSettings::setValue("appStyle", ui->appStyle->toPlainText());
    SqtSettings::setValue("tabSize", ui->tabSize->value());
    SqtSettings::setValue("pgSingleRowMode", ui->singleRowMode->isChecked());
    accept();
    SqtSettings::load();
}
