#include "misc.h"
#include "qdir.h"

QJsonDocument readJsonFile(const QString &path)
{
    QJsonDocument jdoc;
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        auto text_data = file.readAll();
        if (!text_data.isEmpty())
        {
            QJsonParseError error;
            jdoc = QJsonDocument::fromJson(text_data, &error);
            if (error.error != QJsonParseError::NoError)
                throw error.errorString();
        }
        file.close();
    }
    return jdoc;
}
