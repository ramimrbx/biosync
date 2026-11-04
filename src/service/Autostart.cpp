#include "Autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <QSettings>
static const char *RUN_KEY =
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
#endif

const char *Autostart::ENTRY_NAME = "BioSync";

QString Autostart::appPath() {
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

#ifdef Q_OS_WIN

bool Autostart::setEnabled(bool enable) {
    QSettings run(QString::fromLatin1(RUN_KEY), QSettings::NativeFormat);
    if (enable) {
        // Quote the path so a Program Files path with spaces is launched as one argument.
        // --autostart tells main() this is a silent login launch, so it shows the splash but
        // then settles straight into the tray instead of popping the main window open.
        run.setValue(QString::fromLatin1(ENTRY_NAME), QString("\"%1\" --autostart").arg(appPath()));
    } else {
        run.remove(QString::fromLatin1(ENTRY_NAME));
    }
    run.sync();
    return run.status() == QSettings::NoError;
}

bool Autostart::isEnabled() {
    QSettings run(QString::fromLatin1(RUN_KEY), QSettings::NativeFormat);
    const QString value = run.value(QString::fromLatin1(ENTRY_NAME)).toString();
    if (value.isEmpty()) return false;
    // Upgrade path: an entry written before --autostart existed still works (it just shows the
    // main window on login), but rewrite it now so it picks up the tray-only launch behavior.
    if (!value.contains(QStringLiteral("--autostart"))) setEnabled(true);
    return true;
}

#else   // Linux / other: freedesktop autostart entry

static QString desktopFilePath() {
    QString dir = QDir::homePath() + "/.config/autostart";
    return dir + "/biosync.desktop";
}

bool Autostart::setEnabled(bool enable) {
    const QString path = desktopFilePath();
    if (enable) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
        QTextStream out(&f);
        out << "[Desktop Entry]\n"
            << "Type=Application\n"
            << "Name=BioSync\n"
            << "Exec=\"" << appPath() << "\" --autostart\n"
            << "X-GNOME-Autostart-enabled=true\n"
            << "Terminal=false\n";
        f.close();
        return true;
    }
    QFile::remove(path);
    return true;
}

bool Autostart::isEnabled() {
    QFile f(desktopFilePath());
    if (!f.exists()) return false;
    // Upgrade path: an entry written before --autostart opened the window at sign-in; rewrite it so
    // it starts in the tray instead.
    if (f.open(QIODevice::ReadOnly) && !f.readAll().contains("--autostart")) {
        f.close();
        setEnabled(true);
    }
    return true;
}

#endif
