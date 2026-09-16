#pragma once
#include <QObject>
#include "db/Database.h"
#include "server/ZkAdmsServer.h"
#include "service/ApiPusher.h"

// Runs the full BioSync engine (device ADMS server + record store + API pusher) with NO GUI, so it
// can run as a boot-time service before/without any user login. Mirrors the wiring MainWindow does.
class HeadlessRunner : public QObject {
    Q_OBJECT
public:
    explicit HeadlessRunner(QObject *parent = nullptr);
    // Opens the DB, starts the ADMS server and configures the pusher. Returns false only on a fatal
    // startup error (e.g. the DB can't open); a missing API config is non-fatal — it keeps retrying.
    bool start();

private slots:
    void onAttendanceReceived(const QList<AttendanceRecord> &records);
    void onUnknownDeviceConnected(const QString &serial, const QString &ip);
    void onDeviceHeartbeat(const QString &serial, const QString &ip);
    void onLog(const QString &serial, const QString &message);

private:
    void log(const QString &msg);
    void syncKnownSerials();
    void reloadPusherConfig();

    Database     *m_db;
    ZkAdmsServer *m_server;
    ApiPusher    *m_pusher;
    QTimer       *m_configTimer = nullptr;
};
