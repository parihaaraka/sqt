#include "copycontext.h"
#include <QRegularExpression>

void CopyContext::init(const QString &query)
{
    clear();

    // search for comments /*sqt ... */
    QRegularExpression cre(R"(\/\*sqt\s+(.*?)\*\/)", QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator i = cre.globalMatch(query);

    while (i.hasNext())
    {
        QRegularExpressionMatch commentMatch = i.next();
        QString comment = commentMatch.captured(1);

        // search for function-like options within comment
        QRegularExpression ore(R"((^\w+|(?<=\s)\w+)\s*\(([^\)]*))", QRegularExpression::DotMatchesEverythingOption);
        QRegularExpressionMatchIterator oi = ore.globalMatch(comment);
        while (oi.hasNext())
        {
            QRegularExpressionMatch optionMatch = oi.next();
            QString option = optionMatch.captured(1);
            QString value = optionMatch.captured(2).trimmed();
            if (option == "CopyDst")
                _dstFiles.append(value);
            else if (option == "CopySrc")
                _srcFiles.append(value);
        }
    }

    _initialized = true;
}

void CopyContext::clear()
{
    _file.close();
    _srcFiles.clear();
    _dstFiles.clear();
    _curSrcIndex = -1;
    _curDstIndex = -1;
    _initialized = false;
}

bool CopyContext::nextSource()
{
    if (_file.isOpen())
        _file.close();
    if (++_curSrcIndex > _srcFiles.size() - 1)
    {
        emit error("COPY source file is not specified.\n"
                   "  Use special comment to pass source file: /*sqt CopySrc(<file>) */");
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

bool CopyContext::nextDestination()
{
    if (_file.isOpen())
        _file.close();
    if (++_curDstIndex > _dstFiles.size() - 1)
    {
        emit error("COPY destination file is not specified.\n"
                   "  Use special comment to pass destination file: /*sqt CopyDst(<file>) */\n"
                   "  If argument is omitted, then log widget will be used instead of file.");
        return false;
    }

    // use empty argument of CopyDst() to use log widget for output
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

bool CopyContext::write(const char *data, qint64 size)
{
    if (_dstFiles[_curDstIndex].isEmpty())
        emit message(QString::fromStdString(data));
    else
    {
        int bytesWritten = _file.write(data, size);
        if (bytesWritten == size)
            return true;
        else if (bytesWritten < 0)
            emit error(_file.errorString());
        else
            emit error(QString("% bytes out of % were sucessfully written").arg(bytesWritten).arg(size));
        return false;
    }
    return true;
}

bool CopyContext::read(std::vector<char> &data, qint64 maxSize)
{
    if (data.size() < maxSize)
        data.resize(maxSize);
    int size = _file.read(data.data(), maxSize);
    if (size < 0)
    {
        data.resize(0);
        emit error(_file.errorString());
        return false;
    }
    if (size < maxSize)
        data.resize(size);
    return true;
}

CopyContext::operator bool() const
{
    return _initialized;
}
