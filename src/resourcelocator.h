#ifndef RESOURCELOCATOR_H
#define RESOURCELOCATOR_H

#include <QString>
#include <QStringList>

/// Finds the files shipped with the application - scripts, icons - wherever the
/// application happens to be deployed: unpacked into a single folder, installed
/// under windows, or installed system-wide from a distro package.
///
/// The roots are kept in priority order and every lookup walks them in that
/// order. A file, not a folder, is the unit of the search, so a user's copy of a
/// single script shadows the one from the bundle while the rest keeps coming
/// from the bundle.
class ResourceLocator
{
public:
    /// roots are given in priority order, the winning one first
    explicit ResourceLocator(const QStringList &roots) : _roots(roots) {}

    /// The roots of a deployed application: the SQT_ASSETS_DIR override, the
    /// folder chosen by the user, the application's own folder, the per-user
    /// data folder and finally the system-wide one. Missing and duplicate
    /// entries are dropped.
    static ResourceLocator standard(const QString &userDir = QString());

    /// The roots the above is made of, in priority order, before the missing
    /// ones are dropped. Takes its surroundings as arguments instead of asking
    /// the system for them, which is what makes the order testable.
    static QStringList candidateRoots(
            const QString &appDir,
            const QString &userDir = QString(),
            const QString &envDir = QString(),
            const QString &homeDir = QString());

    /// The first existing path for a relative name, empty if there is none.
    QString file(const QString &relativePath) const;
    /// Every existing folder for a relative name, in priority order.
    QStringList dirs(const QString &relativePath) const;

    const QStringList& roots() const noexcept { return _roots; }

private:
    QStringList _roots;
};

/// The instance the application looks its files up through, built on first use.
const ResourceLocator& appResources();

/// Sets the folder of the user's own copies, to be called before the first
/// lookup (the settings are not read here: the locator is also built by the
/// tests, which have no settings at all).
void setAppResourcesUserDir(const QString &userDir);

#endif // RESOURCELOCATOR_H
