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

private:
    QComboBox *_encodingCombo;

signals:
    
public slots:
    
};

#endif // EXTFILEDIALOG_H
