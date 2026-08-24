#ifndef FILESEARCH_H
#define FILESEARCH_H

#include <QObject>
#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <QVector>
#include <atomic>

/// Text search through a directory tree of files (the sql scripts folder, as a
/// rule). Deliberately free of gui and db dependencies: everything here is
/// plain Qt Core, so the matching rules are covered by tst_filesearch and the
/// walking part may run in a worker thread.

/// What to search for and where. Copied into the worker thread as a whole, so
/// it must stay a value type.
struct FileSearchParams
{
    QString text;
    QString path;
    /// File masks, comma (or semicolon) separated. Empty means every file.
    QString including;
    /// Masks of files *and* directories to skip. An excluded directory is not
    /// descended into at all.
    QString excluding;
    bool caseSensitive = false;
    bool wholeWord = false;
    bool regexp = false;
    bool unicodeProperties = false;
    bool recursive = true;
    /// Encoding to read the files in; empty means "guess" (BOM, then utf-8,
    /// then fallbackEncoding).
    QString encoding;
    QString fallbackEncoding;
    /// Files bigger than this are skipped (0 - no limit). A search through a
    /// scripts folder has no business reading a dump that happens to lie there.
    int maxFileSizeKb = 4096;
    bool skipBinary = true;
    /// Upper bound on the number of hits collected, so that a pattern matching
    /// everything cannot fill the memory.
    int maxHits = 20000;
    /// How much of the line is kept for display.
    int snippetLength = 300;
    /// Texts of the files being edited in the application, by absolute name.
    /// A modified editor tab is what the user sees, so it is what gets searched
    /// - otherwise the results disagree with the buffer on screen.
    QHash<QString, QString> bufferTexts;
};
Q_DECLARE_METATYPE(FileSearchParams)

/// A single match. Positions are in characters of the decoded text, with the
/// line endings normalized to '\n' (see FileSearch::readFile), which is exactly
/// what the editor showing the same text sees.
struct FileSearchHit
{
    QString fileName;   ///< absolute
    int line = 1;       ///< 1-based
    int column = 1;     ///< 1-based
    int position = 0;   ///< offset from the beginning of the text
    int length = 0;     ///< match length (may span several lines)
    QString snippet;    ///< the match's line, blanks trimmed, cut to fit
    int snippetOffset = 0;      ///< where the match starts inside the snippet
    int snippetLength = 0;      ///< how much of the match the snippet holds
    bool snippetTrimmed = false;///< the snippet does not start the line
    /// What the text was decoded with. Carried along so that opening the file
    /// in an editor reproduces the very text the search read - any other
    /// encoding would show different characters, and line/column would point
    /// somewhere else. Empty when the hit came from an editor's buffer.
    QString encoding;
};
Q_DECLARE_METATYPE(FileSearchHit)
Q_DECLARE_METATYPE(QVector<FileSearchHit>)

/// What a finished (or aborted) search amounts to.
struct FileSearchSummary
{
    int filesScanned = 0;
    int filesMatched = 0;
    int hits = 0;
    qint64 elapsedMs = 0;
    bool cancelled = false;
    bool truncated = false;     ///< maxHits reached, the rest was not looked at
    QString error;
};
Q_DECLARE_METATYPE(FileSearchSummary)

namespace FileSearch
{

/// A glob translated to an anchored regular expression. Written by hand rather
/// than taken from QRegularExpression::wildcardToRegularExpression() because
/// the latter changed its treatment of '/' between Qt versions, and the mask
/// semantics here must not depend on the Qt the build happened to find.
/// In \a pathMode '*' crosses directory separators, otherwise it does not.
QString globToRegExp(const QString &glob, bool pathMode);

/// A set of masks as typed into one of the panel's fields.
///
/// A mask holding '/' is matched against the entry's path relative to the
/// search root ("tree/*.sql"), any other one against the entry's own name
/// ("*.sql"). A mask without wildcards starting with a dot is understood as an
/// extension (".sql" == "*.sql"), everything else has to match in full.
/// Matching is case insensitive - these are file names, and the same bundle is
/// used on windows.
class MaskSet
{
public:
    static MaskSet fromString(const QString &masks);
    bool isEmpty() const { return _names.isEmpty() && _paths.isEmpty(); }
    /// \a name is the entry's own name, \a relPath its path relative to the
    /// search root (with '/' as the separator, no leading slash).
    bool matches(const QString &name, const QString &relPath) const;

private:
    QVector<QRegularExpression> _names, _paths;
};

/// The pattern the parameters describe, or an invalid expression with \a error
/// filled in. A plain (non-regexp) search is escaped into a pattern as well, so
/// that both kinds share one matching path.
QRegularExpression buildPattern(const FileSearchParams &params, QString *error = nullptr);

/// The outcome of reading one file.
struct FileText
{
    QString text;
    QString encoding;   ///< what the text was decoded with
    bool binary = false;
    bool tooBig = false;
    QString error;
};

/// Reads and decodes a file whole. The line endings are normalized to '\n', so
/// that a hit's line/column suit the editor which will show the very same text
/// (and CRLF files do not report positions shifted by the '\r's).
FileText readFile(const QString &fileName, const FileSearchParams &params);

/// Decodes \a data the way readFile() does, without touching the disk.
FileText decode(const QByteArray &data, const FileSearchParams &params);

/// Every match of \a re in \a text, in order of appearance.
QVector<FileSearchHit> findHits(
        const QString &fileName,
        const QString &text,
        const QRegularExpression &re,
        int snippetLength = 300,
        int maxHits = -1);

} // namespace FileSearch

/// Walks the tree and reports the hits in batches. Lives in a thread of its
/// own; the parameters arrive by value, so nothing is shared with the gui but
/// the cancellation counter.
class FileSearchWorker : public QObject
{
    Q_OBJECT
public:
    explicit FileSearchWorker(QObject *parent = nullptr);

    /// Abandons every search whose generation is \a generation or less. Safe to
    /// call from another thread at any time - this is the only way to stop a
    /// running search, and the reason a search does not have to be waited for
    /// before the next one is started.
    void cancelUpTo(quint64 generation);

public slots:
    /// Invoked through a queued connection. \a generation labels the request,
    /// so that results of an abandoned search are easy to drop.
    void search(FileSearchParams params, quint64 generation);

signals:
    void batch(quint64 generation, QVector<FileSearchHit> hits);
    void progress(quint64 generation, int filesScanned, int filesMatched, int hits);
    void finished(quint64 generation, FileSearchSummary summary);

private:
    bool cancelled(quint64 generation) const;
    std::atomic<quint64> _cancelBelow { 0 };
};

#endif // FILESEARCH_H
