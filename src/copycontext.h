#ifndef COPYCONTEXT_H
#define COPYCONTEXT_H

#include <QStringList>
#include <QFile>

class PgCopyContext : public QObject
{
    Q_OBJECT
public:
    PgCopyContext() = default;
    void init(const QString &query);
    void clear();
    bool nextSource();
    bool nextDestination();
    operator bool() const;
    bool write(const char *data, qint64 size);
    bool read(std::vector<char> &data, size_t maxSize);
private:
    QFile _file;
    QStringList _srcFiles;
    QStringList _dstFiles;
    int _curSrcIndex;
    int _curDstIndex;
    bool _initialized = false;
signals:
    void message(const QString &msg);
    void error(const QString &msg);
};

#endif // COPYCONTEXT_H
