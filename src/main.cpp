#include <QApplication>
#include <QCoreApplication>
#include <QFont>
#include <QPalette>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QIcon>
#include <QWindow>
#include <QProcess>
#include "MainWindow.h"
#include "HeadlessRunner.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// ── Linux desktop integration ─────────────────────────────────────────────────
// The taskbar/dock on Linux (X11 and Wayland) reads the icon from a .desktop
// file, not from the window's icon property. We install the icon and .desktop
// file into the user's XDG local directories on first launch (or whenever the
// files are missing/outdated), then refresh the icon cache.
#ifdef Q_OS_LINUX
static void installDesktopIntegration() {
    // 1. Write icon to ~/.local/share/icons/hicolor/256x256/apps/biosync.png
    const QString iconDir  = QDir::homePath() + "/.local/share/icons/hicolor/256x256/apps";
    const QString iconPath = iconDir + "/biosync.png";
    QDir().mkpath(iconDir);
    if (!QFile::exists(iconPath)) {
        QFile::copy(":/assets/images/icon.ico", iconPath);
        // copy() preserves Qt resource permissions (read-only) — make it writable
        QFile::setPermissions(iconPath,
            QFileDevice::ReadOwner | QFileDevice::WriteOwner |
            QFileDevice::ReadGroup | QFileDevice::ReadOther);
    }

    // 2. Write ~/.local/share/applications/biosync.desktop
    const QString desktopDir  = QDir::homePath() + "/.local/share/applications";
    const QString desktopPath = desktopDir + "/biosync.desktop";
    QDir().mkpath(desktopDir);

    const QString exePath = QCoreApplication::applicationFilePath();
    const QString contents =
        "[Desktop Entry]\n"
        "Version=1.0\n"
        "Type=Application\n"
        "Name=BioSync\n"
        "GenericName=Attendance Manager\n"
        "Comment=ZKTeco biometric attendance manager\n"
        "Icon=biosync\n"
        "Exec=" + exePath + "\n"
        "Terminal=false\n"
        "Categories=Office;Utility;\n"
        "StartupWMClass=BioSync\n"
        "StartupNotify=true\n";

    QFile f(desktopPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write(contents.toUtf8());
        f.close();
    }

    // 3. Refresh desktop and icon caches so the change is visible immediately
    QProcess::startDetached("update-desktop-database", {desktopDir});
    QProcess::startDetached("gtk-update-icon-cache",
        {"-f", "-t", QDir::homePath() + "/.local/share/icons/hicolor"});
}
#endif

int main(int argc, char *argv[]) {
    // Headless / service mode: run the full sync engine with NO GUI, so it can run at boot before
    // any login (systemd on Linux, a boot task/service on Windows). Detected before any QApplication
    // is built, so no display/session is ever required.
    bool headless = false;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == "--headless" || a == "--service" || a == "-H") headless = true;
    }
    if (headless) {
        QCoreApplication::setApplicationName("BioSync");
        QCoreApplication::setOrganizationName("Right iTech");
        QCoreApplication::setApplicationVersion("1.1.0");
        QCoreApplication app(argc, argv);
        HeadlessRunner runner;
        if (!runner.start()) return 1;
        return app.exec();
    }

    // Must be set before QApplication construction — Wayland uses this as app-id
    // to look up the matching .desktop file for the taskbar icon.
    QApplication::setApplicationName("BioSync");
    QApplication::setOrganizationName("Right iTech");

#ifdef Q_OS_WIN
    // Named mutex the installer's AppMutex checks, so an in-place upgrade can detect a
    // running instance and ask the user to close it before replacing files.
    CreateMutexW(nullptr, FALSE, L"BioSyncSingleInstanceMutex");
#endif
    QApplication::setApplicationVersion("1.1.0");
    QApplication::setDesktopFileName("biosync");   // matches biosync.desktop

    QApplication app(argc, argv);
    app.setStyle("Fusion");

#ifdef Q_OS_LINUX
    installDesktopIntegration();
#endif

    const QIcon appIcon(":/assets/images/icon.ico");
    app.setWindowIcon(appIcon);

    // Base font
    QFont appFont("Segoe UI", 10);
    appFont.setHintingPreference(QFont::PreferFullHinting);
    app.setFont(appFont);

    // Global palette
    QPalette p;
    p.setColor(QPalette::Window,          QColor("#F4F6FB"));
    p.setColor(QPalette::WindowText,      QColor("#1A1A2E"));
    p.setColor(QPalette::Base,            QColor("#FFFFFF"));
    p.setColor(QPalette::AlternateBase,   QColor("#F8F9FC"));
    p.setColor(QPalette::Text,            QColor("#1A1A2E"));
    p.setColor(QPalette::BrightText,      QColor("#FFFFFF"));
    p.setColor(QPalette::Button,          QColor("#FFFFFF"));
    p.setColor(QPalette::ButtonText,      QColor("#1A1A2E"));
    p.setColor(QPalette::Highlight,       QColor("#8222E3"));
    p.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));
    p.setColor(QPalette::ToolTipBase,     QColor("#1A0A33"));
    p.setColor(QPalette::ToolTipText,     QColor("#FFFFFF"));
    p.setColor(QPalette::PlaceholderText, QColor("#9CA3AF"));
    app.setPalette(p);

    MainWindow window;
    window.show();

    // Set icon on the native window handle (X11 _NET_WM_ICON property)
    if (QWindow *handle = window.windowHandle())
        handle->setIcon(appIcon);

    return app.exec();
}
