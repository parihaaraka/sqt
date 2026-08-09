#include "settingsdialog.h"
#include "ui_settingsdialog.h"
#include "settings.h"
#include <QPushButton>
#include <QFileDialog>

SettingsDialog::SettingsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SettingsDialog)
{
    ui->setupUi(this);
    restoreGeometry(SqtSettings::value("settingsDialogGeometry").toByteArray());
    ui->appStyle->setPlainText(SqtSettings::value("appStyle").toString());
    ui->encodings->setText(SqtSettings::value("encodings").toString());
    ui->indentSize->setValue(SqtSettings::value("indentSize", 3).toInt());
    ui->singleRowMode->setChecked(SqtSettings::value("pgSingleRowMode", false).toBool());
    ui->highlightCurrentLine->setChecked(SqtSettings::value("highlightCurrentLine", false).toBool());
    ui->tabsIndent->setChecked(SqtSettings::value("tabsIndent", true).toBool());
    ui->visualizeWhitespace->setChecked(SqtSettings::value("visualizeWhitespace", false).toBool());
    ui->lowercaseKeywords->setChecked(SqtSettings::value("lowercaseKeywords", false).toBool());
    ui->f1url->setText(SqtSettings::value("f1url").toString());
    ui->shiftF1url->setText(SqtSettings::value("shiftF1url").toString());
    ui->assetsDir->setText(SqtSettings::value("assetsDir").toString());
}

void SettingsDialog::on_assetsDirBtn_clicked()
{
    const QString dir = QFileDialog::getExistingDirectory(
                this, tr("assets directory"), ui->assetsDir->text());
    // an empty result means the choice was cancelled, not cleared
    if (!dir.isEmpty())
        ui->assetsDir->setText(dir);
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

void SettingsDialog::on_okBtn_clicked()
{
    SqtSettings::setValue("appStyle", ui->appStyle->toPlainText());
    SqtSettings::setValue("encodings", ui->encodings->text());
    SqtSettings::setValue("indentSize", ui->indentSize->value());
    SqtSettings::setValue("pgSingleRowMode", ui->singleRowMode->isChecked());
    SqtSettings::setValue("highlightCurrentLine", ui->highlightCurrentLine->isChecked());
    SqtSettings::setValue("tabsIndent", ui->tabsIndent->isChecked());
    SqtSettings::setValue("visualizeWhitespace", ui->visualizeWhitespace->isChecked());
    SqtSettings::setValue("lowercaseKeywords", ui->lowercaseKeywords->isChecked());
    SqtSettings::setValue("f1url", ui->f1url->text());
    SqtSettings::setValue("shiftF1url", ui->shiftF1url->text());
    SqtSettings::setValue("assetsDir", ui->assetsDir->text());
    SqtSettings::setValue("settingsDialogGeometry", saveGeometry());
    accept();
    SqtSettings::load();
}
