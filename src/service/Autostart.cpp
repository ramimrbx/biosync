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
        run.setValue(QString::fromLatin1(ENTRY_NAME), QString("\"%1\"").arg(appPath()));
    } else {
        run.remove(QString::fromLatin1(ENTRY_NAME));
    }
    run.sync();
    return run.status() == QSettings::NoError;
}

bool Autostart::isEnabled() {
    QSettings run(QString::fromLatin1(RUN_KEY), QSettings::NativeFormat);
    return run.contains(QString::fromLatin1(ENTRY_NAME));
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
            << "Exec=\"" << appPath() << "\"\n"
            << "X-GNOME-Autostart-enabled=true\n"
            << "Terminal=false\n";
        f.close();
        return true;
    }
    QFile::remove(path);
    return true;
}

bool Autostart::isEnabled() {
    return QFile::exists(desktopFilePath());
}

#endif
