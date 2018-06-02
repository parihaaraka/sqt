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
    ui->encodings->setText(SqtSettings::value("encodings").toString());
    ui->tabSize->setValue(SqtSettings::value("tabSize").toInt());
    ui->singleRowMode->setChecked(SqtSettings::value("pgSingleRowMode", false).toBool());
    ui->highlightCurrentLine->setChecked(SqtSettings::value("highlightCurrentLine", false).toBool());
    ui->f1url->setText(SqtSettings::value("f1url").toString());
    ui->shiftF1url->setText(SqtSettings::value("shiftF1url").toString());
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

void SettingsDialog::on_okBtn_clicked()
{
    SqtSettings::setValue("appStyle", ui->appStyle->toPlainText());
    SqtSettings::setValue("encodings", ui->encodings->text());
    SqtSettings::setValue("tabSize", ui->tabSize->value());
    SqtSettings::setValue("pgSingleRowMode", ui->singleRowMode->isChecked());
    SqtSettings::setValue("highlightCurrentLine", ui->highlightCurrentLine->isChecked());
    SqtSettings::setValue("f1url", ui->f1url->text());
    SqtSettings::setValue("shiftF1url", ui->shiftF1url->text());
    accept();
    SqtSettings::load();
}
