#ifndef COPYCONTEXT_H
#define COPYCONTEXT_H

#include <QStringList>
#include <QFile>

class CopyContext : public QObject
{
    Q_OBJECT
public:
    CopyContext() = default;
    void init(const QString &query);
    void clear();
    bool nextSource();
    bool nextDestination();
    operator bool() const;
    bool write(const char *data, qint64 size);
    bool read(std::vector<char> &data, qint64 maxSize);
private:
    QFile _file;
    QStringList _srcFiles;
    QStringList _dstFiles;
    int _curSrcIndex;
    int _curDstIndex;
    bool _initialized = false;
signals:
    void message(const QString &msg) const;
    void error(const QString &msg) const;
};

#endif // COPYCONTEXT_H
