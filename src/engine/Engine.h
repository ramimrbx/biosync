#pragma once
#include <QObject>
#include <QHash>
#include "db/Database.h"
#include "server/ZkAdmsServer.h"
#include "service/ApiPusher.h"

class QTimer;

// The attendance engine: ADMS server (devices connect here) + local record store + API pusher +
// device command delivery. Exactly ONE BioSync process runs it at a time, because only one can
// own the server port. Normally that is the background service; the window runs it only when no
// service is running, and hands it back as soon as the service asks for it.
//
// Everything the engine knows is written to the shared database, so a window that is NOT running
// the engine can still show live device status, punches, logs and send device commands.
//
// Shared-DB keys this class maintains (settings table):
//   engine_role / engine_port / engine_heartbeat  — who runs the server, on which port, last alive (ms)
//   api_online                                    — "1" when RiTEMS was reachable on the last check
// and reads:
//   config_rev              — bumped by Settings → Save; triggers a config reload
//   engine_restart_request  — bumped by the window's Restart button; restarts the server
//   server_port             — rebinds automatically when it changes
class Engine : public QObject {
    Q_OBJECT
public:
    enum class Role { Service, Gui };

    Engine(Database *db, Role role, QObject *parent = nullptr);

    bool start();             // binds the configured port; false if that failed (see below)
    void stop();
    bool isRunning() const;
    quint16 port() const;
    QString roleName() const { return m_role == Role::Service ? "service" : "window"; }

    bool lastErrorWasAddressInUse() const;
    QString lastErrorString() const;

    static constexpr qint64 HEARTBEAT_MS = 2000;

signals:
    void runningChanged(bool running, quint16 port);
    void logMessage(const QString &serial, const QString &message);
    void deviceRegistered(const QString &serial, const QString &ip);
    void attendanceStored(int count);

private:
    void tick();
    void applyConfig();
    void restartServer();
    void writeStatus(bool alive);
    void log(const QString &serial, const QString &message);

    void onAttendanceReceived(const QList<AttendanceRecord> &records);
    void onDeviceHeartbeat(const QString &serial, const QString &ip);
    void onUnknownDevice(const QString &serial, const QString &ip);
    void onTemplatesReceived(const QString &serial, const QList<BiometricTemplate> &templates);
    void onCommandResult(const QString &serial, int commandId, int returnCode);

    Database     *m_db;
    Role          m_role;
    ZkAdmsServer *m_server;
    ApiPusher    *m_pusher;
    QTimer       *m_tick;

    QString m_configRev;
    QString m_restartSeen;
    int     m_ticks = 0;
    QHash<QString, qint64> m_lastSeen;   // serial → ms, to log "came online" once per session
};
