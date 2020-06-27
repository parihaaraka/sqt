#include "copycontext.h"
#include <QRegularExpression>
#include "queryoptions.h"

void PgCopyContext::init(const QString &query)
{
    clear();
    auto qo = QueryOptions::Extract(query);
    if (qo.contains("copy_src"))
        _srcFiles = qo["copy_src"].toVariant().toStringList();
    if (qo.contains("copy_dst"))
        _dstFiles = qo["copy_dst"].toVariant().toStringList();
    _initialized = true;
}

void PgCopyContext::clear()
{
    _file.close();
    _srcFiles.clear();
    _dstFiles.clear();
    _curSrcIndex = -1;
    _curDstIndex = -1;
    _initialized = false;
}

bool PgCopyContext::nextSource()
{
    if (_file.isOpen())
        _file.close();
    if (++_curSrcIndex > _srcFiles.size() - 1)
    {
        emit error(tr("COPY source file is not specified.\n"
                      "  Use special comment to pass source file: /*sqt { \"copy_src\": [\"<file1>\", \"<file2>\"...] } */"
                      "  In case of single COPY FROM query non-array form is allowed: /*sqt { \"copy_src\": \"<file>\" } */\n"));
        return false;
    }
    _file.setFileName(_srcFiles[_curSrcIndex]);
    if (!_file.open(QIODevice::ReadOnly))
    {
        emit error(_file.errorString());
        return false;
    }
    return true;
}

bool PgCopyContext::nextDestination()
{
    if (_file.isOpen())
        _file.close();
    if (++_curDstIndex > _dstFiles.size() - 1)
    {
        emit error(tr("COPY destination file is not specified.\n"
                      "  Use special comment to pass destination files: /*sqt { \"copy_dst\": [\"<file1>\", \"<file2>\"...] } */\n"
                      "  In case of single COPY TO query non-array form is allowed: /*sqt { \"copy_dst\": \"<file>\" } */\n"
                      "  Specify an empty string instead of file name for output to the log widget."));
        return false;
    }

    // empty string as file name => use log widget for output
    if (_dstFiles[_curDstIndex].isEmpty())
        return true;

    _file.setFileName(_dstFiles[_curDstIndex]);
    if (!_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        emit error(_file.errorString());
        return false;
    }
    return true;
}

bool PgCopyContext::write(const char *data, qint64 size)
{
    if (_dstFiles[_curDstIndex].isEmpty())
        emit message(QString::fromStdString(data));
    else
    {
        qint64 bytesWritten = _file.write(data, size);
        if (bytesWritten == size)
            return true;
        else if (bytesWritten < 0)
            emit error(_file.errorString());
        else
            emit error(tr("% bytes out of % were sucessfully written").arg(bytesWritten).arg(size));
        return false;
    }
    return true;
}

bool PgCopyContext::read(std::vector<char> &data, size_t maxSize)
{
    if (data.size() < maxSize)
        data.resize(maxSize);
    qint64 size = _file.read(data.data(), static_cast<qint64>(maxSize));
    if (size < 0)
    {
        data.resize(0);
        emit error(_file.errorString());
        return false;
    }
    if (static_cast<size_t>(size) < maxSize)
        data.resize(static_cast<size_t>(size));
    return true;
}

PgCopyContext::operator bool() const
{
    return _initialized;
}
