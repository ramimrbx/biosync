#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHash>
#include <QQueue>
#include <QDateTime>
#include <QAbstractSocket>
#include <functional>
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
// id is sent to the device as the command ID ("C:<id>:<command>") and echoed back in its result.
struct QueuedCommand {
    int     id = 0;
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

    // Why the last start() failed (meaningful only after it returned false).
    QAbstractSocket::SocketError lastError() const { return m_lastError; }
    QString lastErrorString() const { return m_lastErrorString; }

    void setKnownSerials(const QSet<QString> &serials);
    void addKnownSerial(const QString &serial);

    // Text returned on GET /biosync/ping, so another BioSync process can tell that the port is
    // held by BioSync (and not by some other program).
    void setIdentity(const QString &identity) { m_identity = identity; }

    // Supplies the next command for a device when it polls (returns false when there is none).
    using CommandProvider = std::function<bool(const QString &serial, QueuedCommand &out)>;
    void setCommandProvider(CommandProvider provider) { m_commandProvider = std::move(provider); }

    // Body of a DATA UPDATE FINGERTMP command that pushes templates back to a device.
    static QString buildTemplateRestoreBody(const QList<BiometricTemplate> &templates);

signals:
    void attendanceReceived(const QList<AttendanceRecord> &records);
    void deviceHeartbeat(const QString &serial, const QString &ip);
    void unknownDeviceConnected(const QString &serial, const QString &ip);
    void logMessage(const QString &deviceSerial, const QString &message);
    void biometricTemplatesReceived(const QString &serial,
                                    const QList<BiometricTemplate> &templates);
    // Device reported the outcome of a command (POST /iclock/devicecmd). returnCode 0 = success.
    void commandResult(const QString &serial, int commandId, int returnCode);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onSocketDisconnected();

private:
    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket*, QByteArray>       m_buffers;
    QSet<QString>                         m_knownSerials;
    CommandProvider                       m_commandProvider;
    QString                               m_identity = "BIOSYNC";
    QAbstractSocket::SocketError          m_lastError = QAbstractSocket::UnknownSocketError;
    QString                               m_lastErrorString;

    void handleRequest(QTcpSocket *socket, const ParsedRequest &req);
    void handleGetRequest(QTcpSocket *socket, const ParsedRequest &req);
    void handleAttendancePost(QTcpSocket *socket, const ParsedRequest &req);
    void handleDeviceInfo(QTcpSocket *socket, const ParsedRequest &req);
    void handleDeviceCmdResult(QTcpSocket *socket, const ParsedRequest &req);

    ParsedRequest parseHttpRequest(const QByteArray &data);
    QList<AttendanceRecord>   parseAttendanceData(const QString &serial,
                                                   const QByteArray &body);
    QList<BiometricTemplate>  parseFingerprintData(const QByteArray &body);
    void sendResponse(QTcpSocket *socket, int status, const QString &body);
    QString queryParam(const ParsedRequest &req, const QString &key) const;
};
