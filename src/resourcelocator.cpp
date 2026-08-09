#include "resourcelocator.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>

// The system-wide location, filled in by the build (CMAKE_INSTALL_FULL_DATADIR).
// Without it only the portable layout is searched, which is exactly right for a
// build tree.
#ifndef SQT_DATA_DIR
#define SQT_DATA_DIR ""
#endif

// The same location relative to the folder of the binary. An installed tree may
// end up somewhere other than the prefix it was configured with - moved by hand,
// installed with --prefix, packed into an AppImage - and then the absolute path
// above points nowhere while this one still holds. The build derives it from the
// install directories; the fallback is the layout those directories default to.
#ifndef SQT_DATA_RELDIR
#define SQT_DATA_RELDIR "../share/sqt"
#endif

QStringList ResourceLocator::candidateRoots(
        const QString &appDir,
        const QString &userDir,
        const QString &envDir,
        const QString &homeDir)
{
    QStringList roots;

    // An explicit override, handy to point a packaged build at a candidate set
    // of scripts before shipping them.
    if (!envDir.isEmpty())
        roots << envDir;

    // The user's own folder wins over everything shipped: this is how a single
    // script gets replaced without touching the bundle. It is also the only such
    // place under windows, where there is no per-user data folder in use.
    if (!userDir.isEmpty())
        roots << userDir;

    if (!appDir.isEmpty())
    {
        // Portable layout: everything next to the binary. Also the layout of a
        // windows installation and of a build tree.
        roots << appDir;
        // A relocatable prefix: <prefix>/bin/sqt looking for <prefix>/share/sqt.
        roots << appDir + '/' + SQT_DATA_RELDIR;
    }

    if (!homeDir.isEmpty())
        roots << homeDir + "/.local/share/sqt";

    if (QString(SQT_DATA_DIR).length())
        roots << QString(SQT_DATA_DIR);

    // The same folder twice would let a file shadow itself.
    QStringList res;
    for (const QString &r: roots)
    {
        const QString path = QDir::cleanPath(r);
        if (!res.contains(path))
            res << path;
    }
    return res;
}

ResourceLocator ResourceLocator::standard(const QString &userDir)
{
    const QString appDir = (QCoreApplication::instance() ?
                                QCoreApplication::applicationDirPath() : QString());
    const QStringList roots = candidateRoots(
                appDir,
                userDir,
                QProcessEnvironment::systemEnvironment().value("SQT_ASSETS_DIR"),
                QDir::homePath());

    // A missing folder would only slow every lookup down.
    QStringList existing;
    for (const QString &r: roots)
    {
        if (QFileInfo(r).isDir())
            existing << r;
    }
    return ResourceLocator(existing);
}

QString ResourceLocator::file(const QString &relativePath) const
{
    for (const QString &root: _roots)
    {
        const QString path = root + '/' + relativePath;
        if (QFileInfo::exists(path))
            return path;
    }
    return QString();
}

QStringList ResourceLocator::dirs(const QString &relativePath) const
{
    QStringList res;
    for (const QString &root: _roots)
    {
        const QString path = root + '/' + relativePath;
        if (QFileInfo(path).isDir())
            res << path;
    }
    return res;
}

namespace
{
QString _userDir;
}

void setAppResourcesUserDir(const QString &userDir)
{
    _userDir = userDir;
}

const ResourceLocator& appResources()
{
    static const ResourceLocator loc = ResourceLocator::standard(_userDir);
    return loc;
}
