#pragma once
#include <QMainWindow>
#include <QStackedWidget>
#include <QLabel>
#include <QButtonGroup>
#include <QAbstractButton>
#include <QSystemTrayIcon>
#include <functional>
#include "db/Database.h"
#include "model/BiometricTemplate.h"
#include "engine/Engine.h"
#include "service/ApiPusher.h"
#include "service/DeviceDiscovery.h"
#include "ui/DevicesPage.h"
#include "ui/AttendancePage.h"
#include "ui/LogsPage.h"
#include "ui/SettingsPage.h"
#include "ui/BiometricBackupDialog.h"

class NavButton;
class QNetworkAccessManager;
class QPushButton;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

public:
    enum class Tone { Ok, Warn, Error, Muted };   // status-bar colour, resolved from the theme

    // Bring the window to the front (used when BioSync is launched a second time).
    void showAndRaise();

private slots:
    void onLogMessage(const QString &deviceSerial, const QString &message);
    void onSettingsSaved();
    void onNavItemChanged(int row);
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onScanNetwork();
    void onActivateDevice(int deviceId);
    void onDeactivateDevice(int deviceId);
    void onReconnectDevice(const QString &serial);
    void onRestartDevice(const QString &serial);
    void onBackupTemplates(const QString &serial);
    void onRestoreTemplates(const QString &serial);
    void onServerStart();
    void onServerStop();
    void onServerRestart();
    void onShowAbout();
    void onCheckForUpdates();       // manual "Check for updates" (shows result either way)
    void onRetryFailedRequested();
    void onRetryOneRequested(int recordId);
    void applyTheme();

private:
    // Who runs the attendance server right now, from this window's point of view.
    enum class ServerMode {
        Starting,
        Engine,          // this window owns the port (no background service running)
        Service,         // the BioSync background service owns it; this window mirrors its status
        ServiceWaiting,  // the service wants the port but something else is holding it
        Stopped,         // stopped by the user in Settings
        Conflict         // another (non-BioSync) program holds the port
    };

    Database      *m_db;
    Engine        *m_engine;   // runs only when no background service does
    ApiPusher     *m_pusher;   // used for fetching templates only — never pushes punches

    ServerMode m_mode = ServerMode::Starting;
    bool    m_userStopped = false;
    bool    m_probing = false;
    QTimer *m_superviseTimer = nullptr;
    QTimer *m_refreshTimer = nullptr;
    QNetworkAccessManager *m_probeNam = nullptr;
    int     m_lastAttendanceId = -1;
    int     m_lastPending = -1;
    int     m_lastDeviceCount = -1;

    QStackedWidget *m_stack;
    QAbstractButton *m_navBtns[4];
    QWidget *m_sidebar;
    QWidget *m_logoArea;
    QLabel *m_logoMark;
    QLabel *m_logoTitle;
    QPushButton *m_versionBtn;
    QWidget *m_contentArea;
    QLabel *m_statusServer;
    QLabel *m_statusApi;
    QLabel *m_statusPending;
    QLabel *m_statusDot1;
    QLabel *m_statusDot2;

    DevicesPage    *m_devicesPage;
    AttendancePage *m_attendancePage;
    LogsPage       *m_logsPage;
    SettingsPage   *m_settingsPage;

    QSystemTrayIcon *m_trayIcon = nullptr;
    QHash<QString, bool>                m_deviceOnlineMap;
    QHash<QString, BiometricBackupDialog*> m_backupDialogs;

    void setupUi();
    void setupTray();
    void reloadApiConfig();

    // Every few seconds: decide whether this window should run the server, mirror the service,
    // or hand the port over. Never fights the background service for the port.
    void supervise();
    void probePort(quint16 port, std::function<void(bool isBioSync, QString who)> done);
    void setMode(ServerMode mode, quint16 port, const QString &detail = {});
    // Every 2s: refresh device status, punches, pending count and logs from the shared DB.
    void refreshFromDb();

    // Update checking: queries <api_url>/api/v1/app-releases/latest and, if a newer version exists,
    // offers to download + launch the installer. interactive=false stays silent when up to date.
    void checkForUpdates(bool interactive);
    void promptAndDownloadUpdate(const QString &version, const QString &changelog,
                                 const QString &downloadUrl, const QString &fileName);
    QNetworkAccessManager *m_updateNam = nullptr;
    void setStatusServer(const QString &text, Tone tone);
    void setStatusApi(const QString &text, Tone tone);
    Tone m_serverTone = Tone::Muted;
    Tone m_apiTone = Tone::Muted;
};
