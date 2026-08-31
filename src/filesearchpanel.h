#ifndef FILESEARCHPANEL_H
#define FILESEARCHPANEL_H

#include <QJsonDocument>
#include <QWidget>
#include <memory>
#include "filesearch.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QThread;
class QToolButton;
class QTreeView;
class FileSearchModel;
class FileSearchItemDelegate;
class DbConnection;

/// "Find in files": the second tab next to the object tree.
///
/// Searching a folder of sql scripts is a separate job from searching the
/// database: the scripts are the source, and they routinely differ from what is
/// deployed. The panel keeps its own settings (they are the user's working set,
/// not a one-off), runs the walk in a worker thread and shows the hits as a
/// two-level tree. It knows nothing about the connection - the window holds the
/// one that was current when the search was invoked, and uses it for
/// highlighting and for the editor tab.
class FileSearchPanel : public QWidget
{
    Q_OBJECT
public:
    explicit FileSearchPanel(QWidget *parent = nullptr);
    ~FileSearchPanel();

    /// Puts \a text into the search field and selects it, so that typing
    /// replaces it (F3-like re-search of the previous text still works).
    void setSearchText(const QString &text);
    /// Focus for the search field - what Ctrl+Shift+F ends with.
    void activateSearchField();
    /// The hit under the cursor of the results tree, if any.
    std::optional<FileSearchHit> currentHit() const;

    /// The folder the last search ran in - the root the results' paths are shown
    /// relative to. The window needs it to report a previewed hit's path the
    /// same way, see QueryWidget::codeLocation().
    QString searchRoot() const;

    /// Texts of the modified editor tabs, by absolute file name. Called right
    /// before a search starts, since an unsaved tab is what the user sees.
    void setBufferProvider(std::function<QHash<QString, QString>()> provider);

    /// Binds the panel to the connection the search was invoked from. Each one
    /// keeps its own root folder: the scripts of a database live in their own
    /// repository, and having to re-point the search after every switch is
    /// exactly the tedium this panel exists to remove. \a key identifies the
    /// connection (an opaque digest - see MainWindow::searchProfileKey), \a
    /// label is what a human reads in the settings file and in the tooltip.
    void setConnectionProfile(const QString &key, const QString &label);

    /// The hl.conf of the connection the search was invoked from, so that the
    /// results are colored like the code in the editor next to them. Optional:
    /// without it the colors come from the palette alone.
    void setHighlightSettings(const QJsonDocument &settings);

    /// The hue the results tree marks a match with, for the preview pane to mark
    /// the same hit with. See FileSearchItemDelegate::matchColor.
    QColor matchColor() const;

    void loadSettings();
    void saveSettings();

    /// The panel must be free to follow the splitter down to a sliver: its own
    /// hint is the widest of the rows, and Qt refuses to shrink a widget below
    /// that. The fields elide instead.
    QSize minimumSizeHint() const override;

signals:
    /// The current row has changed: show this place in the content pane.
    void hitSelected(FileSearchHit hit);
    /// Enter/double click: show it and give the pane the focus.
    void hitActivated(FileSearchHit hit);
    /// Ctrl+E: open the file in an editor tab at this position.
    void openInEditorRequested(FileSearchHit hit);
    /// For the bottom log (a failure) and the status bar (a remark).
    void message(const QString &msg);
    void error(const QString &msg);
    void statusMessage(const QString &msg, int msecs);

public slots:
    void startSearch();
    void stopSearch();

private slots:
    void onBatch(quint64 generation, QVector<FileSearchHit> hits);
    void onProgress(quint64 generation, int filesScanned, int filesMatched, int hits);
    void onFinished(quint64 generation, FileSearchSummary summary);
    void onCurrentChanged(const QModelIndex &current);
    void onActivated(const QModelIndex &index);
    void browsePath();
    void showResultsContextMenu(const QPoint &pos);

private:
    void buildUi();
    FileSearchParams currentParams() const;
    /// Keeps the value at the top of the combo's history, capped.
    static void pushHistory(QComboBox *combo, const QString &value);
    static QStringList historyOf(const QComboBox *combo);
    static void setHistory(QComboBox *combo, const QStringList &items, const QString &current);
    void updateStatus(const QString &text);
    /// The single Find/Stop button follows _running: icon and tooltip.
    void updateSearchButton();
    /// Where \a index points, ready to be pasted into an ai agent's prompt:
    /// "sub/file.sql:42" for a match row, "sub/file.sql" for a file row (a whole
    /// file has no one line to name). Relative to the folder that was searched
    /// unless \a absolute, or when the file lies outside it. Empty for an index
    /// that names no file.
    QString locationOf(const QModelIndex &index, bool absolute) const;
    /// locationOf() for the row the keyboard is on, onto the clipboard, with a
    /// remark in the status bar - the clipboard gives no feedback of its own.
    void copyLocationToClipboard(const QModelIndex &index, bool absolute);
    /// Settings key of the path for the current connection, empty when there is
    /// none (then the shared "last used" path is all we have).
    QString pathKey() const;
    /// Remembers the current path as this connection's root.
    void savePathForConnection();

    QComboBox *_text = nullptr;
    QComboBox *_path = nullptr;
    QComboBox *_including = nullptr;
    QComboBox *_excluding = nullptr;
    QCheckBox *_caseSensitive = nullptr;
    QCheckBox *_wholeWord = nullptr;
    QCheckBox *_regexp = nullptr;
    QCheckBox *_regexpU = nullptr;
    QCheckBox *_recursive = nullptr;
    /// Starts the search, or stops the running one - one button, two meanings,
    /// as with "execute query" in the toolbar.
    QToolButton *_searchBtn = nullptr;
    QLabel *_status = nullptr;
    QTreeView *_results = nullptr;
    FileSearchModel *_model = nullptr;
    FileSearchItemDelegate *_delegate = nullptr;

    QThread *_thread = nullptr;
    FileSearchWorker *_worker = nullptr;
    /// Every request gets a number; results of anything but the last one are
    /// dropped. This is what makes a restart instantaneous - the previous walk
    /// finishes on its own without the gui waiting for it.
    quint64 _generation = 0;
    bool _running = false;

    std::function<QHash<QString, QString>()> _bufferProvider;

    /// Identity of the connection whose root folder is shown, and its readable
    /// name. Empty until the window binds one.
    QString _profileKey;
    QString _profileLabel;

protected:
    bool eventFilter(QObject *target, QEvent *event) override;
};

#endif // FILESEARCHPANEL_H
