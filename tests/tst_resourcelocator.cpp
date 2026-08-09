#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include "resourcelocator.h"

/// The point of the locator is the priority of the roots and the fact that a
/// single file can be replaced without replacing the whole folder, so that is
/// what the cases below are about.
class TestResourceLocator : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir _tmp;

    QString root(const QString &name) const { return _tmp.path() + '/' + name; }

    void put(const QString &relativePath)
    {
        const QString path = _tmp.path() + '/' + relativePath;
        QDir().mkpath(QFileInfo(path).path());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(relativePath.toUtf8());   // the content names its own origin
    }

    QString content(const QString &path) const
    {
        QFile f(path);
        return (f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString());
    }

private slots:
    void initTestCase()
    {
        QVERIFY(_tmp.isValid());
        // a bundle with two scripts and an icon
        put("bundle/scripts/postgres/content/table.sql");
        put("bundle/scripts/postgres/content/view.sql");
        put("bundle/decor/table.png");
        // the user replaced one of the scripts and added one of their own
        put("user/scripts/postgres/content/table.sql");
        put("user/scripts/postgres/content/mine.sql");
    }

    void firstRootWins()
    {
        const ResourceLocator loc({root("user"), root("bundle")});
        QCOMPARE(content(loc.file("scripts/postgres/content/table.sql")),
                 QString("user/scripts/postgres/content/table.sql"));
    }

    /// the rest of the bundle keeps coming from the bundle
    void untouchedFilesFallThrough()
    {
        const ResourceLocator loc({root("user"), root("bundle")});
        QCOMPARE(content(loc.file("scripts/postgres/content/view.sql")),
                 QString("bundle/scripts/postgres/content/view.sql"));
        QCOMPARE(content(loc.file("decor/table.png")),
                 QString("bundle/decor/table.png"));
    }

    void missingFileGivesEmptyString()
    {
        const ResourceLocator loc({root("user"), root("bundle")});
        QVERIFY(loc.file("scripts/postgres/content/nowhere.sql").isEmpty());
    }

    /// script folders are enumerated, not asked for by name, so every existing
    /// one is reported in priority order
    void dirsAreListedInOrder()
    {
        const ResourceLocator loc({root("user"), root("bundle")});
        const QStringList dirs = loc.dirs("scripts/postgres/content");
        QCOMPARE(dirs.count(), 2);
        QVERIFY(dirs[0].startsWith(root("user")));
        QVERIFY(dirs[1].startsWith(root("bundle")));

        // a folder only one root has
        const QStringList decor = loc.dirs("decor");
        QCOMPARE(decor.count(), 1);
        QVERIFY(decor[0].startsWith(root("bundle")));

        QVERIFY(loc.dirs("scripts/oracle").isEmpty());
    }

    void reversedPriorityReversesTheResult()
    {
        const ResourceLocator loc({root("bundle"), root("user")});
        QCOMPARE(content(loc.file("scripts/postgres/content/table.sql")),
                 QString("bundle/scripts/postgres/content/table.sql"));
        // a file only the user has is still found
        QCOMPARE(content(loc.file("scripts/postgres/content/mine.sql")),
                 QString("user/scripts/postgres/content/mine.sql"));
    }

    void noRootsFindNothing()
    {
        const ResourceLocator loc({});
        QVERIFY(loc.file("scripts/postgres/content/table.sql").isEmpty());
        QVERIFY(loc.dirs("scripts").isEmpty());
    }

    /// a root that does not exist must not shadow the ones that do
    void absentRootIsSkipped()
    {
        const ResourceLocator loc({root("nowhere"), root("bundle")});
        QCOMPARE(content(loc.file("scripts/postgres/content/table.sql")),
                 QString("bundle/scripts/postgres/content/table.sql"));
        QCOMPARE(loc.dirs("scripts/postgres/content").count(), 1);
    }

    /// <prefix>/bin/sqt has to find <prefix>/share/sqt: an installed tree may
    /// be moved as a whole, and then the path compiled into the binary is of no
    /// use, while the one relative to the binary still holds.
    void dataFolderIsFoundThroughThePrefix()
    {
        put("prefix/share/sqt/scripts/postgres/content/table.sql");
        QVERIFY(QDir().mkpath(root("prefix/bin")));

        const QStringList roots = ResourceLocator::candidateRoots(root("prefix/bin"));
        QVERIFY2(roots.contains(QDir::cleanPath(root("prefix/share/sqt"))),
                 qPrintable("roots: " + roots.join(", ")));

        const ResourceLocator prefixLoc(roots);
        QCOMPARE(content(prefixLoc.file("scripts/postgres/content/table.sql")),
                 QString("prefix/share/sqt/scripts/postgres/content/table.sql"));
    }

    /// the order is the whole point of the list, so it is spelled out here
    void rootsComeInPriorityOrder()
    {
        const QStringList roots = ResourceLocator::candidateRoots(
                    "/app", "/user", "/env", "/home/someone");
        QCOMPARE(roots[0], QString("/env"));
        QCOMPARE(roots[1], QString("/user"));
        QCOMPARE(roots[2], QString("/app"));
        // then the data folder of the prefix the binary sits in, and the
        // per-user folder after it
        QVERIFY2(roots[3].contains("share"), qPrintable(roots[3]));
        QVERIFY(roots.indexOf("/home/someone/.local/share/sqt") > 3);
    }

    /// nothing is invented out of an empty environment
    void absentSurroundingsGiveNoRoots()
    {
        QVERIFY(ResourceLocator::candidateRoots(QString()).isEmpty());
    }

    /// standard() drops what is missing and never repeats a root
    void standardRootsAreUsableAndUnique()
    {
        const ResourceLocator loc = ResourceLocator::standard(root("user"));
        const QStringList roots = loc.roots();
        QCOMPARE(roots.count(), QSet<QString>(roots.begin(), roots.end()).count());
        for (const QString &r: roots)
            QVERIFY2(QFileInfo(r).isDir(), qPrintable("not a folder: " + r));
        // the folder given by the user exists, so it has to be there - first,
        // since no override is set in the environment
        QVERIFY(!roots.isEmpty());
        QCOMPARE(roots.first(), QDir::cleanPath(root("user")));
    }
};

QTEST_APPLESS_MAIN(TestResourceLocator)

#include "tst_resourcelocator.moc"
