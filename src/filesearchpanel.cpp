#include "filesearchpanel.h"
#include "filesearchmodel.h"
#include "settings.h"
#include "styling.h"
#include "textcodec.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QThread>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace
{
/// How many entries each of the editable combos remembers.
constexpr int HistoryDepth = 12;
/// Files are expanded automatically while there are not too many of them; past
/// that the tree is more useful collapsed.
constexpr int AutoExpandLimit = 50;

const char *SettingsGroup = "fileSearch/";
}

FileSearchPanel::FileSearchPanel(QWidget *parent) : QWidget(parent)
{
    qRegisterMetaType<FileSearchParams>();
    qRegisterMetaType<FileSearchHit>();
    qRegisterMetaType<QVector<FileSearchHit>>();
    qRegisterMetaType<FileSearchSummary>();

    buildUi();
    loadSettings();

    // The walk blocks on the disk, so it cannot live in the gui thread. The
    // worker outlives every single search: it is only cancelled, never
    // destroyed, and the thread stops with the panel.
    _thread = new QThread(this);
    _worker = new FileSearchWorker;
    _worker->moveToThread(_thread);
    connect(_thread, &QThread::finished, _worker, &QObject::deleteLater);
    connect(_worker, &FileSearchWorker::batch, this, &FileSearchPanel::onBatch);
    connect(_worker, &FileSearchWorker::progress, this, &FileSearchPanel::onProgress);
    connect(_worker, &FileSearchWorker::finished, this, &FileSearchPanel::onFinished);
    _thread->start();
}

FileSearchPanel::~FileSearchPanel()
{
    // The fields are saved on every search too, but the options may well have
    // been toggled without running one - and they are expected to be there
    // after a restart all the same.
    saveSettings();

    // Tell the walk to stop before waiting for it: the loop checks the counter
    // on every file, so it returns promptly even in the middle of a huge tree.
    _worker->cancelUpTo(_generation);
    _thread->quit();

    // Bounded, then detached. quit() cannot interrupt a search() that is already
    // executing and the cancellation is only polled between files, so a readAll()
    // of a file on an unresponsive mount can hold the worker for an unbounded
    // time - which the gui thread must not wait out with the window already
    // gone. Nothing the worker touches belongs to this panel (the parameters
    // arrive by value, and results go through queued connections that a
    // destroyed receiver drops), so letting the pair finish and delete itself is
    // safe. Unparenting first is required: ~QObject would otherwise delete a
    // still-running QThread and abort.
    if (!_thread->wait(2000))
    {
        _thread->setParent(nullptr);
        connect(_thread, &QThread::finished, _thread, &QObject::deleteLater);
        _thread = nullptr;
        _worker = nullptr;
    }
}

void FileSearchPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(2, 2, 2, 2);
    root->setSpacing(3);

    auto makeCombo = [this](const QString &placeholder) {
        auto *combo = new QComboBox(this);
        combo->setEditable(true);
        combo->setInsertPolicy(QComboBox::NoInsert);
        combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        // Left alone, a combo asks for the width of its longest history entry
        // and never gives it back - which is what kept the whole panel from
        // narrowing with the splitter. The popup still shows the entries in
        // full; only the collapsed field elides.
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setIconSize(QSize(0, 0));    // no icons here, so no room for them
        combo->setMinimumContentsLength(8);
        combo->lineEdit()->setPlaceholderText(placeholder);
        // Enter is handled in the event filter, not through returnPressed():
        // by the time that signal reaches a slot, the combo's completer has
        // already replaced the typed text with the history entry it considers
        // current - so a seeded word would silently turn into the previous
        // search. The filter sees the key first and consumes it.
        combo->installEventFilter(this);
        combo->lineEdit()->installEventFilter(this);
        return combo;
    };

    auto makeCheck = [this](const QString &text, const QString &tip) {
        auto *cb = new QCheckBox(text, this);
        cb->setToolTip(tip);
        return cb;
    };

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(3);

    _text = makeCombo(tr("text to find"));
    _path = makeCombo(tr("folder to search in"));
    _including = makeCombo(tr("*.sql, *.qs"));
    _excluding = makeCombo(tr("nothing skipped"));

    // Spelled out the way Qt Creator does, because that is the meaning users
    // arrive with: "include" is a file pattern, and it is the one field that
    // normally has something in it. Excluding is for the odd folder in the way
    // (a build directory, a vendored copy) rather than for file types - with
    // "*.sql" included, listing "*.log" to be skipped changes nothing.
    _including->setToolTip(tr("File pattern: comma separated masks of the files "
                              "to search, e.g. \"*.sql, *.qs\". Empty means every file."));
    _excluding->setToolTip(tr("Exclusion pattern: comma separated masks of files and "
                              "folders to skip, e.g. \"build*, .git, tmp/*\". "
                              "An excluded folder is not descended into."));

    // The same spellings as the editor's find panel, so that the options read
    // the same in both places.
    _caseSensitive = makeCheck("Aa", tr("Case sensitive"));
    _wholeWord = makeCheck(QString::fromUtf8("\xe2\x80\xb9\xe2\x80\xba"), tr("Whole word"));
    _regexp = makeCheck(".*", tr("Regular expression"));
    _regexpU = makeCheck("/u", tr("Use unicode properties"));
    _recursive = makeCheck(tr("subfolders"), tr("Search in subdirectories"));
    _recursive->setChecked(true);

    // One button with two meanings, the way the toolbar's "execute query" works:
    // two of them would leave one greyed out at all times, and the icons say
    // which one it is now without costing the width a caption would.
    _searchBtn = new QToolButton(this);
    _searchBtn->setAutoRaise(true);
    connect(_searchBtn, &QToolButton::clicked, this, [this]() {
        if (_running)
            stopSearch();
        else
            startSearch();
    });
    updateSearchButton();

    auto *pathBtn = new QToolButton(this);
    pathBtn->setText(QString::fromUtf8("\xe2\x80\xa6"));   // ellipsis
    pathBtn->setToolTip(tr("Choose the folder"));
    connect(pathBtn, &QToolButton::clicked, this, &FileSearchPanel::browsePath);

    // Three columns: labels of their natural width, one stretching column for
    // the fields, and a narrow one for the buttons. Every field then starts and
    // ends on the same vertical line and grows with the panel, instead of each
    // row arranging itself independently.
    int row = 0;
    grid->addWidget(new QLabel(tr("Find:"), this), row, 0);
    grid->addWidget(_text, row, 1);
    grid->addWidget(_searchBtn, row, 2);

    // The checkboxes live in a widget of their own rather than directly in the
    // grid: a nested layout keeps the column as wide as its own contents, and
    // that is what dictated the panel's minimum width. A host widget can be
    // allowed to clip instead.
    ++row;
    auto *optsHost = new QWidget(this);
    auto *opts = new QHBoxLayout(optsHost);
    opts->setContentsMargins(0, 0, 0, 0);
    opts->setSpacing(2);
    opts->addWidget(_caseSensitive);
    opts->addWidget(_wholeWord);
    opts->addWidget(_regexp);
    opts->addWidget(_regexpU);
    opts->addSpacing(8);
    opts->addWidget(_recursive);
    opts->addStretch();
    optsHost->setMinimumWidth(0);
    optsHost->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    grid->addWidget(optsHost, row, 1, 1, 2);

    ++row;
    grid->addWidget(new QLabel(tr("Path:"), this), row, 0);
    grid->addWidget(_path, row, 1);
    grid->addWidget(pathBtn, row, 2);

    // Exclude goes under Include rather than beside it: side by side, each got
    // half the width and neither was readable, while the masks are exactly the
    // kind of text that grows. Both stay in the field column, so that all four
    // fields end on the same line as well - spanning the button column would
    // make these two stick out by the width of a button.
    ++row;
    grid->addWidget(new QLabel(tr("Include:"), this), row, 0);
    grid->addWidget(_including, row, 1);

    ++row;
    grid->addWidget(new QLabel(tr("Exclude:"), this), row, 0);
    grid->addWidget(_excluding, row, 1);

    // Only the fields take the extra width; the labels and the buttons keep
    // theirs, so nothing drifts apart when the panel is widened.
    grid->setColumnStretch(0, 0);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 0);
    root->addLayout(grid);

    _results = new QTreeView(this);
    _results->setHeaderHidden(true);
    _results->setUniformRowHeights(true);
    _results->setAllColumnsShowFocus(true);
    _results->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _results->setSelectionBehavior(QAbstractItemView::SelectRows);
    _results->setContextMenuPolicy(Qt::CustomContextMenu);
    _delegate = new FileSearchItemDelegate(_results);
    _results->setItemDelegate(_delegate);
    _model = new FileSearchModel(this);
    _results->setModel(_model);
    _results->installEventFilter(this);
    // The row stays selected while the focus moves to the preview pane or to an
    // editor tab, and some themes paint that unfocused selection nearly the
    // colour of the background - so the place one is looking at gets lost. Kept
    // in step with the theme by the ApplicationPaletteChange branch of
    // eventFilter().
    fixInactiveSelection(_results);
    root->addWidget(_results, 1);

    // The preview follows the cursor, exactly as it does in the object tree:
    // walking the hits with the arrows is the point of the whole panel.
    connect(_results->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex &current, const QModelIndex &) {
        onCurrentChanged(current);
    });
    connect(_results, &QTreeView::activated, this, &FileSearchPanel::onActivated);
    connect(_results, &QWidget::customContextMenuRequested,
            this, &FileSearchPanel::showResultsContextMenu);

    _status = new QLabel(this);
    _status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont statusFont = _status->font();
    statusFont.setPointSizeF(statusFont.pointSizeF() * 0.9);
    _status->setFont(statusFont);
    root->addWidget(_status);

    setTabOrder(_text, _path);
    setTabOrder(_path, _including);
    setTabOrder(_including, _excluding);
}

QSize FileSearchPanel::minimumSizeHint() const
{
    // The layout's minimum is the widest row - the option checkboxes in a row,
    // in practice - and the splitter honours it, so the panel could not be
    // narrowed past it however much the fields were willing to elide. The
    // height is left to the layout; only the width is released.
    QSize hint = QWidget::minimumSizeHint();
    hint.setWidth(qMin(hint.width(), fontMetrics().averageCharWidth() * 20));
    return hint;
}

void FileSearchPanel::setBufferProvider(std::function<QHash<QString, QString>()> provider)
{
    _bufferProvider = std::move(provider);
}

QString FileSearchPanel::pathKey() const
{
    if (_profileKey.isEmpty())
        return QString();
    return SettingsGroup + QString("roots/") + _profileKey;
}

void FileSearchPanel::savePathForConnection()
{
    const QString key = pathKey();
    if (key.isEmpty() || _path->currentText().isEmpty())
        return;
    SqtSettings::setValue(key, _path->currentText());
    // The label travels with the path so that the settings file stays readable:
    // the key itself is a digest and says nothing about which database it is.
    SqtSettings::setValue(key + "/label", _profileLabel);
}

void FileSearchPanel::setConnectionProfile(const QString &key, const QString &label)
{
    if (key == _profileKey)
    {
        _profileLabel = label;      // same connection, database may have changed
        return;
    }

    // The path shown so far belongs to the previous connection; store it under
    // that one before switching, or switching away would lose it.
    savePathForConnection();

    _profileKey = key;
    _profileLabel = label;

    const QString stored = (key.isEmpty() ? QString() :
                                            SqtSettings::value(pathKey()).toString());
    if (!stored.isEmpty())
        _path->setCurrentText(stored);
    // Nothing stored for this connection: the field keeps what it has. That is
    // the last folder searched anywhere, which is a better guess than nothing -
    // and the first search from here makes it this connection's root.

    _path->setToolTip(label.isEmpty() ? tr("Root folder of the search") :
                                        tr("Root folder for %1").arg(label));
}

void FileSearchPanel::setHighlightSettings(const QJsonDocument &settings)
{
    _delegate->setHighlightSettings(settings);
    // The rows on screen were painted with the previous palette.
    if (_model->fileCount())
        _results->viewport()->update();
}

QColor FileSearchPanel::matchColor() const
{
    return _delegate->matchColor();
}

QStringList FileSearchPanel::historyOf(const QComboBox *combo)
{
    QStringList items;
    for (int i = 0; i < combo->count(); ++i)
        items << combo->itemText(i);
    return items;
}

void FileSearchPanel::setHistory(QComboBox *combo, const QStringList &items, const QString &current)
{
    combo->clear();
    combo->addItems(items);
    combo->setCurrentText(current);
}

void FileSearchPanel::pushHistory(QComboBox *combo, const QString &value)
{
    if (value.isEmpty())
        return;
    QStringList items = historyOf(combo);
    items.removeAll(value);
    items.prepend(value);
    while (items.size() > HistoryDepth)
        items.removeLast();
    setHistory(combo, items, value);
}

void FileSearchPanel::loadSettings()
{
    auto val = [](const char *key, const QVariant &def = QVariant()) {
        return SqtSettings::value(SettingsGroup + QString(key), def);
    };

    setHistory(_text, val("textMru").toStringList(), val("text").toString());
    setHistory(_path, val("pathMru").toStringList(), val("path").toString());
    // A default worth having on a first run: this is a folder of sql scripts.
    // Exclusion starts out empty on purpose - with a file pattern in place there
    // is nothing left for it to reject, and a folder to skip is something only
    // the user's own layout can name.
    setHistory(_including, val("includingMru").toStringList(),
               val("including", "*.sql").toString());
    setHistory(_excluding, val("excludingMru").toStringList(),
               val("excluding").toString());

    _caseSensitive->setChecked(val("caseSensitive", false).toBool());
    _wholeWord->setChecked(val("wholeWord", false).toBool());
    _regexp->setChecked(val("regexp", false).toBool());
    _regexpU->setChecked(val("regexpU", false).toBool());
    _recursive->setChecked(val("recursive", true).toBool());
}

void FileSearchPanel::saveSettings()
{
    auto set = [](const char *key, const QVariant &value) {
        SqtSettings::setValue(SettingsGroup + QString(key), value);
    };

    set("text", _text->currentText());
    set("path", _path->currentText());
    set("including", _including->currentText());
    set("excluding", _excluding->currentText());
    set("textMru", historyOf(_text));
    set("pathMru", historyOf(_path));
    set("includingMru", historyOf(_including));
    set("excludingMru", historyOf(_excluding));
    set("caseSensitive", _caseSensitive->isChecked());
    set("wholeWord", _wholeWord->isChecked());
    set("regexp", _regexp->isChecked());
    set("regexpU", _regexpU->isChecked());
    set("recursive", _recursive->isChecked());

    // The path is stored twice on purpose: once as "the folder searched last"
    // (the fallback for a connection never searched from before) and once under
    // the current connection, which is what it will be restored from.
    savePathForConnection();
}

void FileSearchPanel::setSearchText(const QString &text)
{
    if (text.isEmpty())
    {
        _text->lineEdit()->selectAll();
        return;
    }
    _text->setCurrentText(text);
    _text->lineEdit()->selectAll();
}

void FileSearchPanel::activateSearchField()
{
    _text->setFocus();
    _text->lineEdit()->selectAll();
}

std::optional<FileSearchHit> FileSearchPanel::currentHit() const
{
    return _model->firstHit(_results->currentIndex());
}

QString FileSearchPanel::searchRoot() const
{
    // The model's root, not the path field: the field may have been retyped
    // since, while the results on screen still belong to the folder they were
    // collected in.
    return _model->rootPath();
}

FileSearchParams FileSearchPanel::currentParams() const
{
    FileSearchParams params;
    params.text = _text->currentText();
    params.path = _path->currentText();
    params.including = _including->currentText();
    params.excluding = _excluding->currentText();
    params.caseSensitive = _caseSensitive->isChecked();
    params.wholeWord = _wholeWord->isChecked();
    params.regexp = _regexp->isChecked();
    params.unicodeProperties = _regexpU->isChecked();
    params.recursive = _recursive->isChecked();

    // One shared rule for the search and for the preview that re-reads a hit -
    // see TextCodec::fallbackEncoding().
    params.fallbackEncoding = TextCodec::fallbackEncoding(
                SqtSettings::value("encodings").toString());

    params.maxFileSizeKb = SqtSettings::value(SettingsGroup + QString("maxFileSizeKb"), 4096).toInt();
    params.maxHits = SqtSettings::value(SettingsGroup + QString("maxHits"), 20000).toInt();
    if (_bufferProvider)
        params.bufferTexts = _bufferProvider();
    return params;
}

void FileSearchPanel::startSearch()
{
    if (_text->currentText().isEmpty())
    {
        emit statusMessage(tr("nothing to search for"), 3000);
        activateSearchField();
        return;
    }

    if (_path->currentText().isEmpty())
    {
        browsePath();
        if (_path->currentText().isEmpty())
            return;
    }

    // A pattern that will not compile is worth saying before the walk starts.
    FileSearchParams params = currentParams();
    QString err;
    FileSearch::buildPattern(params, &err);
    if (!err.isEmpty())
    {
        updateStatus(tr("invalid pattern: %1").arg(err));
        emit statusMessage(tr("invalid pattern: %1").arg(err), 5000);
        return;
    }

    pushHistory(_text, params.text);
    pushHistory(_path, params.path);
    pushHistory(_including, params.including);
    pushHistory(_excluding, params.excluding);
    // Saved now rather than only on exit: these are the settings of the search
    // that is about to run, and a crash should not cost them.
    saveSettings();

    // Whatever is running is abandoned here; it will notice on its next file.
    _worker->cancelUpTo(_generation);
    ++_generation;

    _model->setRootPath(params.path);
    _model->clear();
    _running = true;
    updateSearchButton();
    updateStatus(tr("searching..."));

    QMetaObject::invokeMethod(_worker, "search", Qt::QueuedConnection,
                              Q_ARG(FileSearchParams, params),
                              Q_ARG(quint64, _generation));
}

void FileSearchPanel::stopSearch()
{
    if (!_running)
        return;
    _worker->cancelUpTo(_generation);
    // The state is not reset here: the worker will report finished() with
    // cancelled set, and that is where the buttons go back to normal.
}

void FileSearchPanel::onBatch(quint64 generation, QVector<FileSearchHit> hits)
{
    if (generation != _generation)
        return;     // results of an abandoned search

    // The row this batch starts at, sampled before the model is touched, so the
    // loop below can expand only what this batch brought in. Walking every file
    // row instead would re-open a file the user collapsed while the search is
    // still running.
    const int firstNew = _model->fileCount();
    const bool hadNothing = (_model->hitCount() == 0);
    _model->addHits(hits);

    if (firstNew < AutoExpandLimit)
    {
        // Only the files this batch brought in are expanded, so a tree the user
        // has collapsed by hand stays that way.
        for (int row = firstNew; row < _model->fileCount() && row < AutoExpandLimit; ++row)
            _results->expand(_model->index(row, 0));
    }

    if (hadNothing && _model->hitCount())
    {
        // The first hit is put under the cursor, which also previews it: the
        // usual case is one match, and this saves the trip to the tree.
        const QModelIndex first = _model->index(0, 0);
        if (_model->rowCount(first))
            _results->setCurrentIndex(_model->index(0, 0, first));
        else
            _results->setCurrentIndex(first);
    }
}

void FileSearchPanel::onProgress(quint64 generation, int filesScanned, int filesMatched, int hits)
{
    if (generation != _generation)
        return;
    updateStatus(tr("%1 in %2 of %3 files...")
                 .arg(tr("%n match(es)", "", hits))
                 .arg(filesMatched).arg(filesScanned));
}

void FileSearchPanel::onFinished(quint64 generation, FileSearchSummary summary)
{
    if (generation != _generation)
        return;

    _running = false;
    updateSearchButton();

    if (!summary.error.isEmpty())
    {
        updateStatus(summary.error);
        // A failure belongs in the log, which is shared and always visible.
        emit error(summary.error);
        return;
    }

    QString text = tr("%1 in %2 of %3 files, %4 ms")
            .arg(tr("%n match(es)", "", summary.hits))
            .arg(summary.filesMatched)
            .arg(summary.filesScanned)
            .arg(summary.elapsedMs);
    if (summary.cancelled)
        text += tr(" (stopped)");
    if (summary.truncated)
        text += tr(" (limit reached)");
    updateStatus(text);

    // "Nothing found" is a passing remark, not an error: the status bar, as
    // everywhere else in this window.
    if (!summary.hits && !summary.cancelled)
        emit statusMessage(tr("no matches for \"%1\"").arg(_text->currentText()), 3000);
}

void FileSearchPanel::onCurrentChanged(const QModelIndex &current)
{
    // A file row previews its first hit, so that walking the tree with the
    // arrows never leaves the pane showing something unrelated.
    if (const auto hit = _model->firstHit(current))
        emit hitSelected(*hit);
}

void FileSearchPanel::onActivated(const QModelIndex &index)
{
    if (_model->isFileNode(index) && _model->rowCount(index))
    {
        // Activating a file is about seeing its matches; expand rather than
        // jump, the first hit is already previewed.
        _results->setExpanded(index, !_results->isExpanded(index));
        return;
    }
    if (const auto hit = _model->firstHit(index))
        emit hitActivated(*hit);
}

void FileSearchPanel::browsePath()
{
    const QString start = (_path->currentText().isEmpty() ?
                               QString() : _path->currentText());
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Folder to search in"), start);
    if (!dir.isEmpty())
        _path->setCurrentText(dir);
}

void FileSearchPanel::updateStatus(const QString &text)
{
    _status->setText(text);
}

void FileSearchPanel::updateSearchButton()
{
    // The same two icons the toolbar uses for "execute query"/"stop execution",
    // so the state of the panel reads like the rest of the application.
    _searchBtn->setIcon(QIcon(_running ? ":img/control-stop.png" : ":img/control.png"));
    _searchBtn->setToolTip(_running ? tr("Stop the search (Esc)") :
                                      tr("Start the search (Enter)"));
}

QString FileSearchPanel::locationOf(const QModelIndex &index, bool absolute) const
{
    if (!index.isValid())
        return QString();
    const QString fileName = index.data(FileSearchModel::FileNameRole).toString();
    if (fileName.isEmpty())
        return QString();

    QString path = QFileInfo(fileName).absoluteFilePath();
    if (!absolute)
    {
        // The same root the rows are displayed against, so what is copied is what
        // is read on screen. A file outside it keeps the absolute path: a trail of
        // '..' hops is less useful than the full name.
        const QString root = searchRoot();
        if (!root.isEmpty())
        {
            const QString rel = QDir(QFileInfo(root).absoluteFilePath()).relativeFilePath(path);
            if (!rel.startsWith(".."))
                path = rel;
        }
    }
    path = QDir::toNativeSeparators(path);

    // A file row stands for the whole file, so there is no single line to name;
    // a match row is a place, and the line is the whole point of copying it.
    if (const auto hit = _model->hit(index))
        // One arg() call with both values: chained, a '%' in the path would
        // consume the line-number placeholder and put a path that does not
        // exist on the clipboard - the one thing this command must not do.
        return QString("%1:%2").arg(path, QString::number(hit->line));
    return path;
}

void FileSearchPanel::copyLocationToClipboard(const QModelIndex &index, bool absolute)
{
    const QString location = locationOf(index, absolute);
    if (location.isEmpty())
        return;
    QApplication::clipboard()->setText(location);
    // The clipboard says nothing of its own, and the point of the command is to
    // paste this elsewhere - so confirm it, in the status bar, never a popup.
    emit statusMessage(tr("copied: %1").arg(location), 5000);
}

void FileSearchPanel::showResultsContextMenu(const QPoint &pos)
{
    const QModelIndex index = _results->indexAt(pos);
    QMenu menu(this);

    QAction *open = menu.addAction(tr("Open in editor"));
    open->setShortcut(QKeySequence("Ctrl+E"));
    open->setShortcutVisibleInContextMenu(true);
    open->setEnabled(index.isValid());

    menu.addSeparator();

    // What the two copy items will produce, spelled into their own labels: the
    // wording alone ("copy path") never said whether the line came along, nor
    // which of the two paths one would get. Seeing the actual string removes
    // both questions - and shows there is nothing to copy when there is nothing.
    const QString relative = locationOf(index, false);
    const QString absolute = locationOf(index, true);

    QAction *copyRelative = menu.addAction(relative.isEmpty() ?
                                              tr("Copy location") :
                                              tr("Copy location: %1").arg(relative));
    copyRelative->setShortcut(QKeySequence("Ctrl+Shift+C"));
    copyRelative->setShortcutVisibleInContextMenu(true);
    copyRelative->setEnabled(!relative.isEmpty());

    // Offered separately rather than as a setting: which of the two is wanted
    // depends on where it is being pasted, and both are one click away.
    QAction *copyAbsolute = menu.addAction(absolute.isEmpty() ?
                                              tr("Copy full path") :
                                              tr("Copy full path: %1").arg(absolute));
    copyAbsolute->setEnabled(!absolute.isEmpty());
    // Hidden when it would repeat the item above verbatim (the search root is
    // the file's own folder, or there is no root at all).
    copyAbsolute->setVisible(absolute != relative);

    menu.addSeparator();
    QAction *expandAll = menu.addAction(tr("Expand all"));
    QAction *collapseAll = menu.addAction(tr("Collapse all"));

    QAction *chosen = menu.exec(_results->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;
    if (chosen == open)
    {
        if (const auto hit = _model->firstHit(index))
            emit openInEditorRequested(*hit);
    }
    else if (chosen == copyRelative)
        copyLocationToClipboard(index, false);
    else if (chosen == copyAbsolute)
        copyLocationToClipboard(index, true);
    else if (chosen == expandAll)
        _results->expandAll();
    else if (chosen == collapseAll)
        _results->collapseAll();
}

bool FileSearchPanel::eventFilter(QObject *target, QEvent *event)
{
    if (event->type() == QEvent::ApplicationPaletteChange && target == _results)
    {
        // The theme has been switched under us, so the correction has to be
        // recomputed from the new palette (fixInactiveSelection() always starts
        // from qApp's own one, so this does not stack up).
        fixInactiveSelection(_results);
        return false;
    }

    if (event->type() != QEvent::KeyPress)
        return QWidget::eventFilter(target, event);

    auto *ke = static_cast<QKeyEvent*>(event);

    if (target == _results)
    {
        // Ctrl+E is the window's action; it comes here only when the tree has
        // the focus, and the window asks the panel what to open.
        if (ke->key() == Qt::Key_Escape)
        {
            // First Esc calls off a running search, the next one leads back to
            // the search field - refining the request is what one usually wants
            // after a look at the results, and Esc is how the find panel does it.
            if (_running)
                stopSearch();
            else
                activateSearchField();
            return true;
        }
        // The same key the editor uses for the same thing, so "where is this
        // code" is one gesture wherever the code is being read.
        if (ke->key() == Qt::Key_C &&
            ke->modifiers().testFlag(Qt::ControlModifier) &&
            ke->modifiers().testFlag(Qt::ShiftModifier) &&
            !ke->modifiers().testFlag(Qt::AltModifier))
        {
            copyLocationToClipboard(_results->currentIndex(), false);
            return true;
        }
        return QWidget::eventFilter(target, event);
    }

    // one of the input fields
    switch (ke->key())
    {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        // Consumed here, before the combo box sees it. Left to QComboBox, Return
        // makes its completer commit the history entry it thinks is current, and
        // the text the user is looking at (a word seeded by Ctrl+Shift+F, say) is
        // silently replaced by the previous search - which is then what gets
        // searched for.
        startSearch();
        return true;
    case Qt::Key_Escape:
        if (_running)
        {
            stopSearch();
            return true;
        }
        break;
    case Qt::Key_Down:
        // Down out of a field goes to the results, the way a completer would -
        // type, Enter, Down, and the hits can be walked without the mouse.
        if (!(ke->modifiers() & ~Qt::KeypadModifier) && _model->fileCount())
        {
            _results->setFocus();
            if (!_results->currentIndex().isValid())
                _results->setCurrentIndex(_model->index(0, 0));
            return true;
        }
        break;
    default:
        break;
    }
    return QWidget::eventFilter(target, event);
}
