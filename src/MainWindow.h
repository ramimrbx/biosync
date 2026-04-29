#pragma once
#include <QMainWindow>
#include <QStackedWidget>
#include <QLabel>
#include <QButtonGroup>
#include <QAbstractButton>
#include <QSystemTrayIcon>
#include "db/Database.h"
#include "model/BiometricTemplate.h"
#include "server/ZkAdmsServer.h"
#include "service/ApiPusher.h"
#include "service/DeviceDiscovery.h"
#include "ui/DevicesPage.h"
#include "ui/AttendancePage.h"
#include "ui/LogsPage.h"
#include "ui/SettingsPage.h"
#include "ui/BiometricBackupDialog.h"

class NavButton;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onAttendanceReceived(const QList<AttendanceRecord> &records);
    void onDeviceHeartbeat(const QString &serial, const QString &ip);
    void onUnknownDeviceConnected(const QString &serial, const QString &ip);
    void onLogMessage(const QString &deviceSerial, const QString &message);
    void onApiStatusChanged(bool online, int pendingCount);
    void onApiRecordsSynced(int count);
    void onApiLogMessage(const QString &msg);
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
    void onBiometricTemplatesReceived(const QString &serial,
                                      const QList<BiometricTemplate> &templates);
    void onServerStart();
    void onServerStop();
    void onServerRestart();

private:
    Database      *m_db;
    ZkAdmsServer  *m_server;
    ApiPusher     *m_pusher;

    QStackedWidget *m_stack;
    QAbstractButton *m_navBtns[4];
    QLabel *m_statusServer;
    QLabel *m_statusApi;
    QLabel *m_statusPending;

    DevicesPage    *m_devicesPage;
    AttendancePage *m_attendancePage;
    LogsPage       *m_logsPage;
    SettingsPage   *m_settingsPage;

    QSystemTrayIcon *m_trayIcon;
    QHash<QString, bool>                m_deviceOnlineMap;
    QHash<QString, BiometricBackupDialog*> m_backupDialogs;

    void setupUi();
    void setupTray();
    void startServer();
    void reloadApiConfig();
    void updateServerStatus(bool running, quint16 port);
    void syncKnownSerials();
    int  autoRegisterDevice(const QString &serial, const QString &ip);
};
