#ifndef EXTFILEDIALOG_H
#define EXTFILEDIALOG_H

#include <QFileDialog>

class QComboBox;

class ExtFileDialog : public QFileDialog
{
    Q_OBJECT
public:
    explicit ExtFileDialog(QWidget *parent = 0);
    QString encoding();
    void setEncoding(const QString &encoding);
    void fillEncodings();

private:
    QComboBox *_encodingCombo;

};

#endif // EXTFILEDIALOG_H
