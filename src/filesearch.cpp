#include "filesearch.h"
#include "textcodec.h"
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>

namespace FileSearch
{

QString globToRegExp(const QString &glob, bool pathMode)
{
    QString re;
    re.reserve(glob.size() * 2 + 4);
    re += '^';
    // '*' must not jump over a directory boundary unless the mask itself speaks
    // of paths; "*.sql" is about a name, "tree/*.sql" about one level below
    // "tree". "**" always crosses, as everywhere else this syntax is used.
    const QString anyRun = (pathMode ? QStringLiteral("[^/]*") : QStringLiteral(".*"));
    for (int i = 0; i < glob.size(); ++i)
    {
        const QChar c = glob.at(i);
        switch (c.unicode())
        {
        case '*':
            if (pathMode && i + 1 < glob.size() && glob.at(i + 1) == '*')
            {
                // "**" - any number of characters, separators included; a
                // trailing "/" of "**/" is swallowed so that "**/x" matches a
                // bare "x" as well
                ++i;
                if (i + 1 < glob.size() && glob.at(i + 1) == '/')
                {
                    ++i;
                    re += QStringLiteral("(?:.*/)?");
                }
                else
                    re += QStringLiteral(".*");
            }
            else
                re += anyRun;
            break;
        case '?':
            re += (pathMode ? QStringLiteral("[^/]") : QStringLiteral("."));
            break;
        case '[':
        {
            // a character class is passed through, with the negation spelled
            // the way globs spell it
            int j = i + 1;
            QString cls;
            if (j < glob.size() && (glob.at(j) == '!' || glob.at(j) == '^'))
            {
                cls += '^';
                ++j;
            }
            bool closed = false;
            for (; j < glob.size(); ++j)
            {
                const QChar cc = glob.at(j);
                if (cc == ']')
                {
                    closed = true;
                    break;
                }
                if (cc == '\\' || cc == '[' || cc == '^')
                    cls += '\\';
                cls += cc;
            }
            if (closed)
            {
                re += '[' + cls + ']';
                i = j;
            }
            else
                re += QStringLiteral("\\[");   // an unbalanced '[' is a literal
            break;
        }
        default:
            re += QRegularExpression::escape(QString(c));
        }
    }
    re += '$';
    return re;
}

MaskSet MaskSet::fromString(const QString &masks)
{
    MaskSet set;
    const QStringList parts = masks.split(QRegularExpression("[,;]"), Qt::SkipEmptyParts);
    for (const QString &raw: parts)
    {
        QString mask = raw.trimmed();
        if (mask.isEmpty())
            continue;

        // typing ".sql" (or "sql") is the same as typing "*.sql": a bare
        // extension is what one reaches for, and it would otherwise match
        // nothing at all
        if (!mask.contains('*') && !mask.contains('?') && !mask.contains('['))
        {
            if (mask.startsWith('.'))
                mask.prepend('*');
        }

        // '\' is a separator on windows and nothing but an escape here
        mask.replace('\\', '/');
        const bool pathMode = mask.contains('/');
        // "dir/" means the directory and everything under it
        if (pathMode && mask.endsWith('/'))
            mask += QStringLiteral("**");

        QRegularExpression re(globToRegExp(mask, pathMode),
                              QRegularExpression::CaseInsensitiveOption);
        if (!re.isValid())
            continue;
        if (pathMode)
            set._paths.append(re);
        else
            set._names.append(re);
    }
    return set;
}

bool MaskSet::matches(const QString &name, const QString &relPath) const
{
    for (const QRegularExpression &re: _names)
    {
        if (re.match(name).hasMatch())
            return true;
    }
    if (_paths.isEmpty())
        return false;

    QString path = relPath;
    path.replace('\\', '/');
    while (path.startsWith('/'))
        path.remove(0, 1);
    for (const QRegularExpression &re: _paths)
    {
        if (re.match(path).hasMatch())
            return true;
        // a path mask also matches everything inside what it names, so that
        // excluding "build" or "a/b" prunes the whole subtree
        if (re.match(path.section('/', 0, -2)).hasMatch())
            return true;
    }
    return false;
}

QRegularExpression buildPattern(const FileSearchParams &params, QString *error)
{
    if (error)
        error->clear();

    QString pattern = (params.regexp ?
                           params.text :
                           QRegularExpression::escape(params.text));

    if (params.wholeWord && !pattern.isEmpty())
    {
        // Lookarounds around the *grouped* pattern. Grouping is what makes this
        // hold for a regexp: `|` has the lowest precedence in pcre, so a bare
        // boundary glued to the pattern text would bind to one alternative only
        // and "foo|bar" would match the "bar" inside "foobar".
        //
        // Each guard is an alternation, because the requirement is conditional -
        // the case findWholeWordWithNonWordEdges() pins down: a match whose own
        // edge is a non-word character must not demand a word boundary there, or
        // "(x)" would never be found inside "f(x)". So:
        //   left  - nothing word-like immediately before, or the match itself
        //           begins with a non-word character;
        //   right - nothing word-like immediately after, or the match itself
        //           ended with a non-word character.
        // The lookarounds inspect the *matched text*, which is why they hold for
        // a plain string and a regexp alike - the pattern source says nothing
        // about what the pattern can match ("\w+" starts with a backslash).
        pattern = "(?:(?<!\\w)|(?=\\W))(?:" + pattern + ")(?:(?!\\w)|(?<=\\W))";
    }

    QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
    if (!params.caseSensitive)
        options |= QRegularExpression::CaseInsensitiveOption;
    if (params.unicodeProperties)
        options |= QRegularExpression::UseUnicodePropertiesOption;

    QRegularExpression re(pattern, options);
    if (!re.isValid() && error)
        *error = re.errorString();
    // Precompiled here rather than on the first match: the same expression is
    // used for thousands of files, and optimize() is what makes that cheap.
    if (re.isValid())
        re.optimize();
    return re;
}

FileText decode(const QByteArray &data, const FileSearchParams &params)
{
    FileText res;

    // Whether the NUL-byte heuristic below applies at all. It does not for
    // utf-16/utf-32, where every ascii character *is* a byte pair (or quad)
    // holding a NUL - 'a' is "61 00" - so applying it to the raw bytes would
    // reject every such file. Both encodings are offered by TextCodec and both
    // have their BOMs recognised, so they have to reach the decoder.
    auto nulMeansBinary = [](const QString &enc) {
        return !enc.startsWith("UTF-16", Qt::CaseInsensitive) &&
               !enc.startsWith("UTF-32", Qt::CaseInsensitive);
    };
    // A NUL byte is what every editor uses to tell a binary file apart. Only the
    // head is examined, so a big file costs nothing here.
    auto hasNul = [&data]() {
        const int probe = int(qMin<qsizetype>(data.size(), 8192));
        return data.left(probe).contains('\0');
    };

    QString encoding = params.encoding;
    if (encoding.isEmpty() || !encoding.compare("auto", Qt::CaseInsensitive))
    {
        // A file that says what it is is believed, exactly as QueryWidget does
        // when opening one.
        encoding = TextCodec::bomEncoding(data);
        if (encoding.isEmpty())
        {
            // No BOM, so a NUL here does mean binary: what is left to try is
            // utf-8 and the single-byte fallback, and neither can contain one.
            if (params.skipBinary && hasNul())
            {
                res.binary = true;
                return res;
            }

            bool ok = false;
            res.text = TextCodec::decode(data, "UTF-8", &ok);
            if (ok)
            {
                res.encoding = "UTF-8";
                return res;
            }
            // Not utf-8, so it is a single-byte file. Whatever the user reads
            // such files in is the best guess available; latin-1 keeps the
            // bytes intact when there is no setting at all, so the search still
            // works for the ascii part of the text.
            encoding = (params.fallbackEncoding.isEmpty() ?
                            QStringLiteral("ISO-8859-1") : params.fallbackEncoding);
        }
    }

    if (params.skipBinary && nulMeansBinary(encoding) && hasNul())
    {
        res.binary = true;
        return res;
    }

    if (TextCodec::canonicalName(encoding).isEmpty())
    {
        res.error = QObject::tr("unknown encoding: %1").arg(encoding);
        return res;
    }
    res.text = TextCodec::decode(data, encoding);
    res.encoding = encoding;

    // A wide-encoding file still has to be told from a binary one, and its
    // decoded text is where that can be asked: a real NUL *character* has no
    // place in text, while the NUL bytes of its ascii range do.
    if (params.skipBinary && !nulMeansBinary(encoding) &&
        QStringView{res.text}.left(8192).contains(QChar(u'\0')))
    {
        res.text.clear();
        res.encoding.clear();
        res.binary = true;
    }
    return res;
}

FileText readFile(const QString &fileName, const FileSearchParams &params)
{
    FileText res;
    QFile f(fileName);
    if (params.maxFileSizeKb > 0 && f.size() > qint64(params.maxFileSizeKb) * 1024)
    {
        res.tooBig = true;
        return res;
    }
    if (!f.open(QIODevice::ReadOnly))
    {
        res.error = f.errorString();
        return res;
    }
    const QByteArray data = f.readAll();
    if (f.error() != QFileDevice::NoError)
    {
        res.error = f.errorString();
        return res;
    }
    f.close();

    res = decode(data, params);
    // The editor that will show this file normalizes the line endings the same
    // way (QPlainTextEdit keeps no '\r'), so the positions reported here are
    // the positions it will use.
    res.text.replace("\r\n", "\n");
    res.text.replace('\r', '\n');
    return res;
}

QVector<FileSearchHit> findHits(
        const QString &fileName,
        const QString &text,
        const QRegularExpression &re,
        int snippetLength,
        int maxHits)
{
    QVector<FileSearchHit> hits;
    if (!re.isValid() || re.pattern().isEmpty() || text.isEmpty())
        return hits;

    // Line numbers are counted along with the walk instead of being recomputed
    // per match: a file with thousands of hits would otherwise be quadratic.
    int line = 1;
    int lineStart = 0;
    int scanned = 0;

    int offset = 0;
    while (offset <= text.size())
    {
        const QRegularExpressionMatch m = re.match(text, offset);
        if (!m.hasMatch())
            break;

        const int start = int(m.capturedStart());
        const int end = int(m.capturedEnd());

        for (; scanned < start; ++scanned)
        {
            if (text.at(scanned) == '\n')
            {
                ++line;
                lineStart = scanned + 1;
            }
        }

        FileSearchHit hit;
        hit.fileName = fileName;
        hit.line = line;
        hit.column = start - lineStart + 1;
        hit.position = start;
        hit.length = end - start;

        // The snippet is the match's own line: its leading blanks dropped (a
        // deeply indented line would otherwise show nothing but spaces) and its
        // length capped, keeping the match itself inside.
        int lineEnd = int(text.indexOf('\n', start));
        if (lineEnd < 0)
            lineEnd = int(text.size());
        int from = lineStart;
        while (from < start && text.at(from).isSpace())
            ++from;

        int visibleEnd = qMin(lineEnd, from + snippetLength);
        if (visibleEnd < end)
        {
            // A long line whose match sits far to the right: slide the window so
            // that the match is visible, which is the whole point of the snippet.
            from = qMax(from, start - snippetLength / 3);
            visibleEnd = qMin(lineEnd, from + snippetLength);
        }

        hit.snippet = text.mid(from, visibleEnd - from);
        hit.snippetTrimmed = (from > lineStart);
        hit.snippetOffset = start - from;
        // A match running past the end of the line (or of the window) is shown
        // as far as it goes - the tree has one line per hit.
        hit.snippetLength = qMin(end, visibleEnd) - start;
        if (hit.snippetLength < 0)
            hit.snippetLength = 0;
        hits.append(hit);

        if (maxHits > 0 && hits.size() >= maxHits)
            break;

        // An empty match (a pattern like "^" or "\b") would otherwise spin here
        offset = (end > start ? end : start + 1);
    }
    return hits;
}

} // namespace FileSearch

FileSearchWorker::FileSearchWorker(QObject *parent) : QObject(parent)
{
}

void FileSearchWorker::cancelUpTo(quint64 generation)
{
    // Only ever grows, so a late call from the gui thread cannot revive a search
    // that was already abandoned.
    quint64 prev = _cancelBelow.load(std::memory_order_relaxed);
    while (prev < generation &&
           !_cancelBelow.compare_exchange_weak(prev, generation, std::memory_order_relaxed))
    { }
}

bool FileSearchWorker::cancelled(quint64 generation) const
{
    return _cancelBelow.load(std::memory_order_relaxed) >= generation;
}

void FileSearchWorker::search(FileSearchParams params, quint64 generation)
{
    FileSearchSummary summary;
    QElapsedTimer timer;
    timer.start();

    auto done = [&]() {
        summary.elapsedMs = timer.elapsed();
        emit finished(generation, summary);
    };

    if (cancelled(generation))
    {
        summary.cancelled = true;
        done();
        return;
    }

    QString error;
    const QRegularExpression re = FileSearch::buildPattern(params, &error);
    if (!error.isEmpty() || params.text.isEmpty())
    {
        summary.error = (error.isEmpty() ? tr("nothing to search for") : error);
        done();
        return;
    }

    const QDir root(params.path);
    if (params.path.isEmpty() || !root.exists())
    {
        summary.error = tr("directory not found: %1").arg(params.path);
        done();
        return;
    }
    const QString rootPath = root.absolutePath();

    const FileSearch::MaskSet including = FileSearch::MaskSet::fromString(params.including);
    const FileSearch::MaskSet excluding = FileSearch::MaskSet::fromString(params.excluding);

    QVector<FileSearchHit> pending;
    QElapsedTimer sinceEmit;
    sinceEmit.start();
    QElapsedTimer sinceProgress;
    sinceProgress.start();

    // Results go out in batches: a hit at a time would flood the event loop of
    // the gui thread, and a single list at the end would show nothing until the
    // whole tree had been walked.
    auto flush = [&](bool force) {
        if (pending.isEmpty())
            return;
        if (!force && pending.size() < 100 && sinceEmit.elapsed() < 100)
            return;
        emit batch(generation, pending);
        pending.clear();
        sinceEmit.restart();
    };

    // Throttled for the same reason as the batches above, and reported for every
    // file rather than only the matching ones: filesScanned is the number that
    // grows on every file, and emitting it only next to a hit left the panel
    // showing nothing at all while a rare string was hunted through a big tree.
    auto reportProgress = [&](bool force) {
        if (!force && sinceProgress.elapsed() < 100)
            return;
        emit progress(generation, summary.filesScanned, summary.filesMatched, summary.hits);
        sinceProgress.restart();
    };

    QDirIterator::IteratorFlags flags = QDirIterator::NoIteratorFlags;
    if (params.recursive)
        flags |= QDirIterator::Subdirectories;
    // Symlinks are not followed: a link pointing at an ancestor would send the
    // walk in circles, and the scripts folders have no need for them.
    QDirIterator it(rootPath, QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks, flags);

    QHash<QString, bool> dirVerdict;    // pruning decisions, one per directory

    while (it.hasNext())
    {
        const QString fileName = it.next();
        if (cancelled(generation))
        {
            summary.cancelled = true;
            break;
        }

        const QFileInfo fi = it.fileInfo();
        const QString relPath = QDir(rootPath).relativeFilePath(fileName);

        // QDirIterator has no way to skip a directory, so the exclusion is
        // applied to the path: the entries inside a pruned directory are cheap
        // to reject (the verdict is remembered) and never read.
        bool excludedDir = false;
        QString dirRel = relPath.section('/', 0, -2);
        if (!dirRel.isEmpty())
        {
            auto cached = dirVerdict.constFind(dirRel);
            if (cached != dirVerdict.constEnd())
                excludedDir = cached.value();
            else
            {
                QString walked;
                const QStringList parts = dirRel.split('/', Qt::SkipEmptyParts);
                for (const QString &part: parts)
                {
                    walked += (walked.isEmpty() ? QString() : QStringLiteral("/")) + part;
                    // a hidden directory (.git, .svn) is never worth walking
                    if (part.startsWith('.') ||
                        (!excluding.isEmpty() && excluding.matches(part, walked)))
                    {
                        excludedDir = true;
                        break;
                    }
                }
                dirVerdict.insert(dirRel, excludedDir);
            }
        }
        if (excludedDir)
            continue;

        const QString name = fi.fileName();
        if (name.startsWith('.'))
            continue;
        if (!excluding.isEmpty() && excluding.matches(name, relPath))
            continue;
        if (!including.isEmpty() && !including.matches(name, relPath))
            continue;

        ++summary.filesScanned;
        reportProgress(false);

        QString text;
        // Left empty for a buffer: the tab already holds the file and knows
        // what it was read with. fromBuffer records *why* it is empty.
        QString encoding;
        bool fromBuffer = false;
        const auto buffer = params.bufferTexts.constFind(fi.absoluteFilePath());
        if (buffer != params.bufferTexts.constEnd())
        {
            // an editor tab holds this file; what the user sees is what gets
            // searched, saved or not
            text = buffer.value();
            fromBuffer = true;
        }
        else
        {
            const FileSearch::FileText content = FileSearch::readFile(fi.absoluteFilePath(), params);
            if (content.binary || content.tooBig)
                continue;
            if (!content.error.isEmpty())
                continue;   // unreadable files are not worth a message each
            text = content.text;
            encoding = content.encoding;
        }

        const int room = (params.maxHits > 0 ? params.maxHits - summary.hits : -1);
        QVector<FileSearchHit> hits = FileSearch::findHits(
                    fi.absoluteFilePath(), text, re, params.snippetLength, room);
        if (hits.isEmpty())
            continue;
        // So that whoever opens the file reproduces this very text rather than
        // guessing an encoding of their own.
        for (FileSearchHit &hit: hits)
        {
            hit.encoding = encoding;
            hit.fromBuffer = fromBuffer;
        }

        ++summary.filesMatched;
        summary.hits += hits.size();
        pending += hits;
        flush(false);
        // Forced: a new match is worth showing at once, and matches are far
        // rarer than files.
        reportProgress(true);

        if (params.maxHits > 0 && summary.hits >= params.maxHits)
        {
            summary.truncated = true;
            break;
        }
    }

    flush(true);
    reportProgress(true);
    done();
}
