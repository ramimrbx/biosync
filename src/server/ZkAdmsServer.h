#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHash>
#include <QQueue>
#include <QDateTime>
#include "../model/AttendanceRecord.h"
#include "../model/BiometricTemplate.h"

struct ParsedRequest {
    QString method;
    QString path;
    QHash<QString, QString> queryParams;
    QHash<QString, QString> headers;
    QByteArray body;
    bool valid = false;
};

// A command to be delivered to a device on its next getrequest poll.
// body contains optional data lines appended after the command header (e.g. for DATA UPDATE).
struct QueuedCommand {
    QString command;
    QString body;
};

class ZkAdmsServer : public QObject {
    Q_OBJECT
public:
    explicit ZkAdmsServer(QObject *parent = nullptr);

    bool start(quint16 port);
    void stop();
    bool isRunning() const;
    quint16 port() const;

    void setKnownSerials(const QSet<QString> &serials);
    void addKnownSerial(const QString &serial);

    // Queue a plain command (e.g. "REBOOT") for a device.
    void queueCommand(const QString &serial, const QString &command);

    // Queue a DATA QUERY FINGERTMP command — device will POST its templates back.
    void queueBiometricQuery(const QString &serial);

    // Queue a DATA UPDATE FINGERTMP command to push templates back to the device.
    void queueBiometricRestore(const QString &serial,
                               const QList<BiometricTemplate> &templates);

signals:
    void attendanceReceived(const QList<AttendanceRecord> &records);
    void deviceHeartbeat(const QString &serial, const QString &ip);
    void unknownDeviceConnected(const QString &serial, const QString &ip);
    void logMessage(const QString &deviceSerial, const QString &message);
    void biometricTemplatesReceived(const QString &serial,
                                    const QList<BiometricTemplate> &templates);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onSocketDisconnected();

private:
    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket*, QByteArray>       m_buffers;
    QSet<QString>                         m_knownSerials;
    QHash<QString, QQueue<QueuedCommand>> m_commandQueues;
    int m_commandId = 1;

    void handleRequest(QTcpSocket *socket, const ParsedRequest &req);
    void handleGetRequest(QTcpSocket *socket, const ParsedRequest &req);
    void handleAttendancePost(QTcpSocket *socket, const ParsedRequest &req);
    void handleDeviceInfo(QTcpSocket *socket, const ParsedRequest &req);

    ParsedRequest parseHttpRequest(const QByteArray &data);
    QList<AttendanceRecord>   parseAttendanceData(const QString &serial,
                                                   const QByteArray &body);
    QList<BiometricTemplate>  parseFingerprintData(const QByteArray &body);
    void sendResponse(QTcpSocket *socket, int status, const QString &body);
    QString queryParam(const ParsedRequest &req, const QString &key) const;
};
