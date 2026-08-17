#include "extfiledialog.h"
#include <QGridLayout>
#include <QLabel>
#include <QComboBox>
#include "settings.h"
#include "textcodec.h"


ExtFileDialog::ExtFileDialog(QWidget *parent) :
    QFileDialog(parent)
{
    setOption(QFileDialog::DontUseNativeDialog);
    setDefaultSuffix("sql");
    QGridLayout* mainLayout = qobject_cast<QGridLayout*>(layout());

    if (!mainLayout)
        return;
    else
    {
        QHBoxLayout *hbl = new QHBoxLayout();
        _encodingCombo = new QComboBox(this);
        hbl->addStretch();
        hbl->addWidget(new QLabel(tr("Encoding")));
        hbl->addWidget(_encodingCombo);
        int numRows = mainLayout->rowCount();
        mainLayout->addLayout(hbl, numRows,0,1,-1);
    }
}

QString ExtFileDialog::encoding()
{
    return _encodingCombo->currentText();
}

void ExtFileDialog::setEncoding(const QString &encoding)
{
    // The name is canonicalized so that a file remembered as "cp1251" still
    // selects the "windows-1251" the list offers instead of being appended to it
    // as a second spelling of the same thing.
    const QString name = TextCodec::canonicalName(encoding);
    int ind = (name.isEmpty() ? -1 : _encodingCombo->findText(name, Qt::MatchFixedString));
    if (ind == -1)
    {
        if (name.isEmpty())
        {
            if (!_encodingCombo->count())
                _encodingCombo->addItem("UTF-8");
            ind = 0;
        }
        else
        {
            _encodingCombo->addItem(name);
            ind = _encodingCombo->count() - 1;
        }
    }
    _encodingCombo->setCurrentIndex(ind);
}

void ExtFileDialog::fillEncodings()
{
    _encodingCombo->clear();
    const QStringList names = SqtSettings::value("encodings").toString().
            split(',', Qt::SkipEmptyParts);
    for (const QString &name: names)
    {
        // Nothing can convert an encoding we do not know, so offering it would
        // only produce a failure on save. A typo in the settings silently
        // disappears from the list rather than breaking it.
        const QString canonical = TextCodec::canonicalName(name.trimmed());
        if (!canonical.isEmpty() &&
            _encodingCombo->findText(canonical, Qt::MatchFixedString) == -1)
            _encodingCombo->addItem(canonical);
    }
    // An empty list would leave no way to save at all, so fall back to
    // everything we can convert.
    if (!_encodingCombo->count())
        _encodingCombo->addItems(TextCodec::availableEncodings());
    _encodingCombo->setCurrentIndex(0);
}

