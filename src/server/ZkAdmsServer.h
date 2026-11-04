#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHash>
#include <QSet>
#include <QQueue>
#include <QDateTime>
#include <QAbstractSocket>
#include <QJsonObject>
#include "ServerTypes.h"
#include "../model/AttendanceRecord.h"
#include "../model/BiometricTemplate.h"

class RitosLink;

// The attendance server every device connects to. One port serves both protocols:
//   - ZKTeco devices speak ADMS ("push SDK") over plain HTTP: /iclock/cdata, /iclock/getrequest …
//   - RitOS devices (our own BioSync hardware) open a WebSocket on the same port, any path.
//     A request carrying "Upgrade: websocket" is handed to RitosLink, which speaks their
//     JSON or text protocol and raises the very same signals below.
// The rest of BioSync therefore treats both kinds of device identically.
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
    using CommandProvider = ::CommandProvider;
    void setCommandProvider(CommandProvider provider);

    // Body of a DATA UPDATE FINGERTMP command that pushes templates back to a device.
    static QString buildTemplateRestoreBody(const QList<BiometricTemplate> &templates);

    // Reply to a RitOS device's interactive-attendance context request (see RitosLink).
    void sendPunchContext(const QString &serial, int requestId, const QJsonObject &context);

    // PIN → name from ZKTeco user data ("USER PIN=1<TAB>Name=…" in OPERLOG, USERINFO tables or
    // /iclock/querydata "user … pin=1<TAB>name=…").
    static QHash<QString, QString> parseUserNames(const QByteArray &body);

signals:
    void attendanceReceived(const QList<AttendanceRecord> &records);
    void deviceHeartbeat(const QString &serial, const QString &ip);
    void unknownDeviceConnected(const QString &serial, const QString &ip);
    void logMessage(const QString &deviceSerial, const QString &message);
    void biometricTemplatesReceived(const QString &serial,
                                    const QList<BiometricTemplate> &templates);
    // A template backup finished on the device without any templates to send.
    void biometricQueryEmpty(const QString &serial, int commandId);
    // Device reported the outcome of a command. returnCode 0 = success; message is optional.
    void commandResult(const QString &serial, int commandId, int returnCode, const QString &message);
    // Names of people enrolled on a device, as the device reported them.
    void deviceUserNames(const QString &serial, const QHash<QString, QString> &names);
    // A ZKTeco device polled over ADMS (so the engine can ask it for its user list once).
    void admsDevicePolled(const QString &serial);
    // RitOS interactive attendance: a person just punched — what should the device ask them?
    void punchContextRequested(const QString &serial, int requestId, const QString &userPin,
                               const QDateTime &punchTime);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onSocketDisconnected();

private:
    QTcpServer *m_server = nullptr;
    RitosLink  *m_ritos  = nullptr;
    QHash<QTcpSocket*, QByteArray>       m_buffers;
    QSet<QString>                         m_knownSerials;
    CommandProvider                       m_commandProvider;
    QString                               m_identity = "BIOSYNC";
    QAbstractSocket::SocketError          m_lastError = QAbstractSocket::UnknownSocketError;
    QString                               m_lastErrorString;

    void noteDevice(const QString &serial, const QString &ip);
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
