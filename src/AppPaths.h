#pragma once
#include <QString>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

// Where BioSync keeps its database + settings. It MUST be a machine-wide location shared between the
// GUI (run by a desktop user, where config is entered) and the headless service (run at boot as
// root/SYSTEM, before any login), so the service can read the config the user set. Falls back to a
// per-user path only if the shared one is not writable.
namespace AppPaths {

inline QString dataDir() {
#ifdef Q_OS_WIN
    QString base = qEnvironmentVariable("ProgramData");
    if (base.isEmpty()) base = "C:/ProgramData";
    QString shared = QDir::fromNativeSeparators(base) + "/BioSync";
#else
    QString shared = QStringLiteral("/var/lib/biosync");
#endif
    QDir().mkpath(shared);
    QFileInfo fi(shared);
    if (fi.isDir() && fi.isWritable())
        return shared;

    // Fallback: per-user (used if the shared dir isn't writable, e.g. a dev run without install).
    QString user = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (user.isEmpty()) user = QDir::homePath() + "/.biosync";
    QDir().mkpath(user);
    return user;
}

inline QString dbPath() {
    QString path = dataDir() + "/biosync.db";
    // One-time migration from the old per-user location so upgrades keep their config/devices.
    if (!QFile::exists(path)) {
        const QString legacy =
#ifdef Q_OS_WIN
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/biosync.db";
#else
            QDir::homePath() + "/.biosync/biosync.db";
#endif
        if (QFile::exists(legacy) && legacy != path)
            QFile::copy(legacy, path);
    }
    return path;
}

} // namespace AppPaths
