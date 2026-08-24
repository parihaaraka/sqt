#include "filesearchmodel.h"
#include "settings.h"
#include "styling.h"
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>
#include <QTextDocument>
#include <QTreeView>

FileSearchModel::FileSearchModel(QObject *parent) : QAbstractItemModel(parent)
{
}

/// The internal id encodes which of the two levels an index belongs to: 0 for a
/// file row, (file row + 1) for its matches. No node objects are allocated, so a
/// batch of thousands of hits costs one append.
static constexpr quintptr FileLevel = 0;

QModelIndex FileSearchModel::index(int row, int column, const QModelIndex &parent) const
{
    if (row < 0 || column != 0)
        return QModelIndex();

    if (!parent.isValid())
        return (row < _files.size() ? createIndex(row, column, FileLevel) : QModelIndex());

    if (parent.internalId() != FileLevel)
        return QModelIndex();       // a match has no children
    if (parent.row() >= _files.size() || row >= _files[parent.row()].rows.size())
        return QModelIndex();
    return createIndex(row, column, quintptr(parent.row() + 1));
}

QModelIndex FileSearchModel::parent(const QModelIndex &child) const
{
    if (!child.isValid() || child.internalId() == FileLevel)
        return QModelIndex();
    return createIndex(int(child.internalId() - 1), 0, FileLevel);
}

int FileSearchModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return int(_files.size());
    if (parent.internalId() != FileLevel || parent.row() >= _files.size())
        return 0;
    return int(_files[parent.row()].rows.size());
}

int FileSearchModel::columnCount(const QModelIndex &) const
{
    return 1;
}

bool FileSearchModel::isFileNode(const QModelIndex &index) const
{
    return index.isValid() && index.internalId() == FileLevel;
}

/// The text as it is shown, with the html special characters escaped and the
/// tabs turned into spaces (a tab inside a one-line item renders as nothing).
static QString escapeSnippet(const QString &text)
{
    QString res = text;
    res.replace('\t', QStringLiteral("    "));
    return res.toHtmlEscaped();
}

/// The snippet with every mark wrapped in \a tag ("span class=..." or "b").
static QString markUp(const QString &snippet, const QVector<QPair<int, int>> &marks,
                      const QString &openTag, const QString &closeTag)
{
    QString html;
    int pos = 0;
    for (const auto &m: marks)
    {
        if (m.first < pos || m.second <= 0)
            continue;       // overlapping marks would produce nested tags
        html += escapeSnippet(snippet.mid(pos, m.first - pos));
        html += openTag + escapeSnippet(snippet.mid(m.first, m.second)) + closeTag;
        pos = m.first + m.second;
    }
    html += escapeSnippet(snippet.mid(pos));
    return html;
}

QVariant FileSearchModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    if (isFileNode(index))
    {
        if (index.row() >= _files.size())
            return QVariant();
        const FileNode &node = _files[index.row()];
        switch (role)
        {
        case Qt::DisplayRole:
            // the count is worth seeing at a glance, the way every editor shows it
            return QString("%1 (%2)").arg(node.display).arg(node.rows.size());
        case HtmlRole:
            return QString("<span class=\"file\">%1</span> <span class=\"light\">(%2)</span>")
                    .arg(escapeSnippet(node.display)).arg(node.rows.size());
        case FileNameRole:
            return node.fileName;
        case Qt::ToolTipRole:
            return node.fileName;
        default:
            return QVariant();
        }
    }

    const int fileRow = int(index.internalId() - 1);
    if (fileRow < 0 || fileRow >= _files.size() || index.row() >= _files[fileRow].rows.size())
        return QVariant();
    const Row &row = _files[fileRow].rows[index.row()];
    const FileSearchHit &h = row.hit;

    switch (role)
    {
    case Qt::DisplayRole:
        // plain text, so that copying a row yields something usable
        return QString("%1: %2").arg(h.line).arg(h.snippet);
    case HtmlRole:
    {
        // an ellipsis says the line goes on to the left, so that a line number
        // whose text starts mid-way is not a surprise
        const QString lead = (h.snippetTrimmed ? QStringLiteral("&hellip;") : QString());
        return QString("<span class=\"light\">%1:</span> %2%3")
                .arg(QString::number(h.line), lead,
                     markUp(h.snippet, row.marks,
                            QStringLiteral("<span class=\"match\">"),
                            QStringLiteral("</span>")));
    }
    case HitRole:
        return QVariant::fromValue(h);
    case FileNameRole:
        return h.fileName;
    case Qt::ToolTipRole:
    {
        // The tree has no horizontal scrolling, so a long line is cut on screen;
        // the tooltip is where the whole of it can be read - and it arrives
        // faster than scrolling would anyway. The match is bold here, since a
        // tooltip has no style sheet of its own to hang a class on.
        //
        // The column is deliberately absent: it says nothing one cannot see, and
        // the file and the line are what one copies or asks about.
        QString tip = QString("%1:%2").arg(h.fileName).arg(h.line);
        if (row.marks.size() > 1)
            tip += QObject::tr(" (%n match(es))", "", int(row.marks.size()));
        // <pre> would keep the indentation, but it also refuses to wrap; the
        // snippet has its leading blanks trimmed off already.
        return tip + "<br/><br/>" + markUp(h.snippet, row.marks,
                                      QStringLiteral("<b>"), QStringLiteral("</b>"));
    }
    default:
        return QVariant();
    }
}

void FileSearchModel::setRootPath(const QString &path)
{
    _rootPath = path;
}

void FileSearchModel::clear()
{
    beginResetModel();
    _files.clear();
    _fileRow.clear();
    _rowCount = 0;
    endResetModel();
}

bool FileSearchModel::addMark(Row &row, const FileSearchHit &other)
{
    // Where the further match falls inside the snippet already stored. The
    // snippet is a window on the line, so a match beyond its right edge simply
    // has nowhere to be shown - the row still stands for it, and the tooltip
    // says how many there are.
    const int offset = row.hit.snippetOffset + (other.position - row.hit.position);
    if (offset < 0 || offset >= row.hit.snippet.size())
        return false;
    const int length = qMin(other.length, int(row.hit.snippet.size()) - offset);
    if (length <= 0)
        return false;

    const auto &last = row.marks.constLast();
    if (offset < last.first + last.second)
        return false;       // overlaps the previous mark (a self-overlapping pattern)

    row.marks.append({offset, length});
    return true;
}

void FileSearchModel::addHits(const QVector<FileSearchHit> &hits)
{
    if (hits.isEmpty())
        return;

    const QDir root(_rootPath);
    for (const FileSearchHit &hit: hits)
    {
        auto it = _fileRow.constFind(hit.fileName);
        if (it == _fileRow.constEnd())
        {
            const int row = int(_files.size());
            beginInsertRows(QModelIndex(), row, row);
            FileNode node;
            node.fileName = hit.fileName;
            node.display = (_rootPath.isEmpty() ?
                                hit.fileName : root.relativeFilePath(hit.fileName));
            node.rows.append({hit, {{hit.snippetOffset, hit.snippetLength}}});
            _files.append(node);
            _fileRow.insert(hit.fileName, row);
            endInsertRows();
            ++_rowCount;
            continue;
        }

        const int fileRow = it.value();
        FileNode &node = _files[fileRow];
        const QModelIndex parentIndex = index(fileRow, 0);

        // Hits arrive in the order they appear in the file, so another match on
        // the line already shown can only be the last row's - no search needed.
        if (!node.rows.isEmpty() && node.rows.constLast().hit.line == hit.line)
        {
            const int lastRow = int(node.rows.size()) - 1;
            addMark(node.rows[lastRow], hit);
            // The row now marks one more place, and the tooltip counts them.
            const QModelIndex hitIndex = index(lastRow, 0, parentIndex);
            emit dataChanged(hitIndex, hitIndex, {Qt::DisplayRole, HtmlRole, Qt::ToolTipRole});
            continue;
        }

        const int hitRow = int(node.rows.size());
        beginInsertRows(parentIndex, hitRow, hitRow);
        node.rows.append({hit, {{hit.snippetOffset, hit.snippetLength}}});
        endInsertRows();
        ++_rowCount;
        // the count shown on the file row has changed
        emit dataChanged(parentIndex, parentIndex, {Qt::DisplayRole, HtmlRole});
    }
}

std::optional<FileSearchHit> FileSearchModel::hit(const QModelIndex &index) const
{
    if (!index.isValid() || isFileNode(index))
        return std::nullopt;
    const int fileRow = int(index.internalId() - 1);
    if (fileRow < 0 || fileRow >= _files.size() || index.row() >= _files[fileRow].rows.size())
        return std::nullopt;
    return _files[fileRow].rows[index.row()].hit;
}

std::optional<FileSearchHit> FileSearchModel::firstHit(const QModelIndex &index) const
{
    if (!index.isValid())
        return std::nullopt;
    if (!isFileNode(index))
        return hit(index);
    if (index.row() >= _files.size() || _files[index.row()].rows.isEmpty())
        return std::nullopt;
    return _files[index.row()].rows.first().hit;
}

QString FileSearchModel::fileName(const QModelIndex &index) const
{
    return index.data(FileNameRole).toString();
}

// ---------------------------------------------------------------------------

FileSearchItemDelegate::FileSearchItemDelegate(QObject *parent) :
    QStyledItemDelegate(parent)
{
}

void FileSearchItemDelegate::setHighlightSettings(const QJsonDocument &settings)
{
    _hlSettings = settings;
    _css.clear();       // rebuilt on the next paint, with the new palette
}

/// \a c brought to a lightness that reads on \a base without glaring: the
/// editor's own colors are chosen for a text area, and a tree row is smaller
/// and denser, so full contrast there is what made the results look shouty.
static QColor comfortable(QColor c, const QColor &base, qreal strength)
{
    if (!c.isValid())
        return c;
    // Mixing towards the background rather than lowering the alpha: an alpha
    // colour inside a QTextDocument is composited against whatever the delegate
    // painted, and on a selected row that is the selection - the text would fade
    // exactly when it needs to stay readable.
    return QColor::fromRgbF(c.redF()   * strength + base.redF()   * (1 - strength),
                            c.greenF() * strength + base.greenF() * (1 - strength),
                            c.blueF()  * strength + base.blueF()  * (1 - strength));
}

void FileSearchItemDelegate::rebuildCss(const QColor &text, const QColor &base) const
{
    const bool dark = isDarkMode();

    // The regular part of a row is the plain text colour, toned down: a found
    // line is context for the match, not something to be read first. Full
    // QPalette::Text (pure white in a dark theme) is what looked too bright.
    _regular = comfortable(text, base, 0.72);

    // The line number and the file's match count are dimmer still - they are
    // labels, and they repeat on every row.
    const QColor light = comfortable(text, base, 0.45);

    // The file rows carry the name of the script, which is what one scans the
    // tree for, so they keep the full text colour.
    const QColor file = text;

    // The match itself: the "literal" colour of the connection's hl.conf, since
    // that is a hue the editor uses for content rather than for syntax - blue
    // reads as a keyword in sql, which is exactly the wrong association here.
    // hlFormat() applies the dark/light variants and the bundle's overrides for
    // us, so the marked text ends up the same colour the editor would give a
    // string literal in the file being previewed.
    QColor match = hlFormat(_hlSettings["literal"], QVariant(),
                            dark ? QColor("#bc6") : QColor("#c00"))
            .foreground().color();
    if (!match.isValid())
        match = (dark ? QColor("#bc6") : QColor("#c00"));
    // Bold and a touch of the background keep it distinct without the neon.
    match = comfortable(match, base, 0.85);

    _css = QString("span.light { color: %1; } "
                   "span.file { color: %2; } "
                   "span.match { color: %3; font-weight: bold; }")
            .arg(light.name(), file.name(), match.name());
    _cssText = text;
    _cssBase = base;
}

QColor FileSearchItemDelegate::matchColor() const
{
    // The same derivation the marked text in a row gets, minus the toning down
    // against a row background: the preview pane paints this as a translucent
    // background instead of as text, and does its own mixing. What must agree
    // between the two is the hue, and that is what this returns.
    const bool dark = isDarkMode();
    QColor match = hlFormat(_hlSettings["literal"], QVariant(),
                            dark ? QColor("#bc6") : QColor("#c00"))
            .foreground().color();
    if (!match.isValid())
        match = (dark ? QColor("#bc6") : QColor("#c00"));
    return match;
}

void FileSearchItemDelegate::prepareDoc(
        const QStyleOptionViewItem &option, const QModelIndex &index, QTextDocument &doc) const
{
    doc.setDocumentMargin(0);
    QTextOption opt;
    opt.setWrapMode(QTextOption::NoWrap);
    doc.setDefaultTextOption(opt);
    doc.setDefaultFont(option.font);

    // A selected row is painted on the highlight colour, so that is what the
    // text has to be comfortable against; hence the palette role rather than a
    // single "background" fixed once.
    const bool selected = (option.state & QStyle::State_Selected);
    const QColor text = option.palette.color(selected ? QPalette::HighlightedText
                                                      : QPalette::Text);
    const QColor base = option.palette.color(selected ? QPalette::Highlight
                                                      : QPalette::Base);
    if (_css.isEmpty() || text != _cssText || base != _cssBase)
        rebuildCss(text, base);

    doc.setDefaultStyleSheet(_css);
    doc.setHtml(index.data(FileSearchModel::HtmlRole).toString());
}

void FileSearchItemDelegate::initStyleOption(
        QStyleOptionViewItem *option, const QModelIndex &index) const
{
    QStyledItemDelegate::initStyleOption(option, index);
    // The document in paint() draws the row; leaving the text here would have
    // the style draw the very same line as well, at its own vertical offset -
    // which is what made every row look doubled.
    option->text.clear();
}

void FileSearchItemDelegate::paint(
        QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    // Background, selection and focus rect come from the style (initStyleOption()
    // above has taken the text out of its hands).
    QStyledItemDelegate::paint(painter, option, index);

    QStyleOptionViewItem style(option);
    initStyleOption(&style, index);

    QTextDocument doc;
    prepareDoc(style, index, doc);

    // Where the style would have put the text, so that the document lines up
    // with the branch indicator and the selection. The view's own style, since
    // the tree is given a proxy style of its own in this application.
    QStyle *s = (style.widget ? style.widget->style() : QApplication::style());
    QRect textRect = s->subElementRect(QStyle::SE_ItemViewItemText, &style, style.widget);
    if (!textRect.isValid() || textRect.isEmpty())
        textRect = style.rect;

    painter->save();
    painter->setClipRect(style.rect);
    // The document's own default colour (the palette's text) would be used for
    // everything the style sheet does not name; the toned down one is what the
    // unmarked part of a row is meant to be.
    if (_regular.isValid())
        painter->setPen(_regular);
    // One line of html against a row that may be taller (uniform row heights),
    // hence the centering rather than a plain top-left.
    const int dy = qMax(0, int((textRect.height() - doc.size().height()) / 2));
    painter->translate(textRect.topLeft() + QPoint(0, dy));
    // A clip of the row's width, so a long line ends at the edge instead of
    // being drawn over the neighbouring widgets.
    doc.drawContents(painter, QRectF(0, 0, textRect.width(), textRect.height()));
    painter->restore();
}

QSize FileSearchItemDelegate::sizeHint(
        const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyleOptionViewItem style(option);
    initStyleOption(&style, index);

    QTextDocument doc;
    prepareDoc(style, index, doc);
    const QSizeF sz = doc.documentLayout()->documentSize();
    // The height is what matters here (the width is a hint the tree ignores for
    // its only column); a couple of pixels of air keeps the rows from touching.
    return QSize(int(sz.width()) + 8, int(sz.height()) + 2);
}
