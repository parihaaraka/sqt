#ifndef FILESEARCHMODEL_H
#define FILESEARCHMODEL_H

#include <QAbstractItemModel>
#include <QJsonDocument>
#include <QStyledItemDelegate>
#include <QVector>
#include "filesearch.h"

class QTextDocument;

/// The results of a file search as a two-level tree: files on top, the lines
/// they matched below. Rows arrive in batches while the search runs, so the
/// model appends and never rebuilds - the selection and the expanded state
/// survive the whole run.
class FileSearchModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Roles {
        /// FileSearchHit of a match row (invalid QVariant on a file row).
        HitRole = Qt::UserRole + 1,
        /// Absolute file name, on both kinds of row.
        FileNameRole,
        /// The snippet marked up for the delegate.
        HtmlRole
    };

    explicit FileSearchModel(QObject *parent = nullptr);

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    /// The root the paths are shown relative to. Purely cosmetic.
    void setRootPath(const QString &path);
    void clear();
    /// Appends a batch, grouping the hits under their files. Several matches on
    /// one line end up in one row (see Row).
    void addHits(const QVector<FileSearchHit> &hits);

    bool isFileNode(const QModelIndex &index) const;
    /// The hit behind \a index, or std::nullopt for a file row.
    std::optional<FileSearchHit> hit(const QModelIndex &index) const;
    /// The first hit of a file row (what activating a file jumps to).
    std::optional<FileSearchHit> firstHit(const QModelIndex &index) const;
    QString fileName(const QModelIndex &index) const;

    int fileCount() const { return int(_files.size()); }
    /// Rows shown under the files - lines, not matches (the count of matches is
    /// the worker's business, and the status line has it).
    int hitCount() const { return _rowCount; }

private:
    /// One line of one file, however many times the pattern matched in it.
    /// Repeating the line once per match pushes the rest of the results out of
    /// sight, and the line is what one reads anyway; the other matches are
    /// marked inside the very same row instead.
    struct Row
    {
        FileSearchHit hit;
        /// Every match this snippet holds, as (offset, length) within the
        /// snippet. The first one is \a hit's own.
        QVector<QPair<int, int>> marks;
    };

    struct FileNode
    {
        QString fileName;
        QString display;    ///< path relative to the root
        QVector<Row> rows;
    };

    /// Notes \a other - a further match of the row's line - inside \a row.
    /// False when the snippet does not reach that far, so nothing was marked.
    static bool addMark(Row &row, const FileSearchHit &other);

    QVector<FileNode> _files;
    QHash<QString, int> _fileRow;
    QString _rootPath;
    int _rowCount = 0;
};

/// Draws a match row with the found text marked, the way the db tree delegate
/// draws its nodes: a QTextDocument holding a little html, since a plain item
/// can only be painted in one color.
///
/// The colors are the editor's own - taken from the connection's hl.conf, the
/// file the syntax highlighter reads - so that the results read like the code
/// shown next to them. Without a connection they are derived from the palette.
class FileSearchItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit FileSearchItemDelegate(QObject *parent = nullptr);

    /// The dbms bundle's hl.conf, as loaded for the highlighter. An empty
    /// document (no connection, or a bundle without one) is fine.
    void setHighlightSettings(const QJsonDocument &settings);

    /// The hue a match is marked with. Public because the preview pane marks the
    /// same hit in the same color - the tree and the pane are read as one thing,
    /// and two different "found here" colors would be a puzzle to nobody's gain.
    QColor matchColor() const;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

protected:
    /// Drops the plain text from the option: the row is painted by the document
    /// in paint(), and the style must not draw the same line underneath.
    /// It has to be done here rather than on a local copy, because
    /// QStyledItemDelegate::paint() calls this very method on a copy of its own.
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override;

private:
    void prepareDoc(const QStyleOptionViewItem &option, const QModelIndex &index, QTextDocument &doc) const;
    /// Builds the style sheet for \a text on \a base. Cached: the same handful
    /// of colors for every row, and hl.conf would be parsed on each repaint
    /// otherwise.
    void rebuildCss(const QColor &text, const QColor &base) const;

    QJsonDocument _hlSettings;
    mutable QString _css;
    mutable QColor _cssText, _cssBase;
    /// The color the unmarked part of a row is painted with - kept aside so
    /// that the painter's pen can be set to it before the document is drawn.
    mutable QColor _regular;
};

#endif // FILESEARCHMODEL_H
