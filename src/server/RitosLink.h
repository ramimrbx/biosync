#pragma once
#include <QObject>
#include <QHash>
#include <QList>
#include <QSet>
#include <QByteArray>
#include <QJsonObject>
#include <QDateTime>
#include "ServerTypes.h"
#include "../model/AttendanceRecord.h"
#include "../model/BiometricTemplate.h"

class QTcpSocket;
class QTimer;

// RitOS devices (our own BioSync attendance hardware) connect to the attendance server with a
// WebSocket instead of ZKTeco's ADMS HTTP polling. ZkAdmsServer hands such connections over here
// right after reading the upgrade request; this class completes the handshake, speaks the
// RitOS protocol and raises the same events ZkAdmsServer raises for ZKTeco devices.
//
// The device sends JSON or plain text (its "Data format" setting); BioSync answers in kind:
//
//   device → BioSync                                   BioSync → device
//   hello   {"type":"hello","device":"BioSync-…",…}     ack      {"type":"ack","id":35}
//           HELLO,<sn>,<name>,<os>,<ip>,<users>,…               ACK,35
//   punch   {"type":"punch","id":35,"userId":…,"time":…} command  {"type":"command","id":12,
//           PUNCH,35,<userId>,<name>,<time>                       "command":"REBOOT","body":…,"more":false}
//   template{"type":"template","command":12,…,"data":…}           C:12:REBOOT[:MORE]\n<body>
//           TEMPLATE,12,<userId>,<finger>,<size>,<base64>
//   result  {"type":"result","id":12,"return":0,"count":…,"message":…}
//           RESULT,12,<return>,<count>,<command>,<message>
//
// Interactive attendance (answered on the device, so RiTEMS shows no browser/Android popup):
//   context {"type":"context","id":7,"userId":…,"time":…}  context {"type":"context","id":7,
//           CONTEXT,7,<userId>,<time>                            "interactive":true,"autoDirection":…}
//                                                              (always JSON — RiTEMS' punch context)
//   punch   … ,"answer":{"direction":"EXIT","exitType":"OFFICE","reason":…,"mistake":false}
//           ANSWER,<punch id>,<ENTRY|EXIT>,<exit type or ->,<mistake 0/1>,<reason> just before PUNCH
//
// Commands understood by the device: REBOOT, INFO, DATA QUERY FINGERTMP (backup),
// DATA UPDATE FINGERTMP (restore, ADMS "Pin=…\tFId=…\tTmp=…" lines), DATA QUERY ATTLOG (resend).
// A restore body is split into parts that fit the device's 15 KB WebSocket message limit.
class RitosLink : public QObject {
    Q_OBJECT
public:
    explicit RitosLink(QObject *parent = nullptr);

    static bool isUpgradeRequest(const ParsedRequest &req);

    // Takes over a socket whose WebSocket upgrade request has just been read.
    void adopt(QTcpSocket *socket, const ParsedRequest &req, const QByteArray &leftover);
    void closeAll();
    void setCommandProvider(CommandProvider provider) { m_commandProvider = std::move(provider); }

    // Answers a device's "context" request (interactive attendance) with RiTEMS' punch context.
    // Unknown serial or a closed connection is ignored: the device times out and records a plain punch.
    void sendPunchContext(const QString &serial, int requestId, const QJsonObject &context);

signals:
    void deviceSeen(const QString &serial, const QString &ip);
    void attendanceReceived(const QList<AttendanceRecord> &records);
    void logMessage(const QString &deviceSerial, const QString &message);
    void biometricTemplatesReceived(const QString &serial, const QList<BiometricTemplate> &templates);
    void biometricQueryEmpty(const QString &serial, int commandId);
    void commandResult(const QString &serial, int commandId, int returnCode, const QString &message);
    // A person just punched on a device with interactive attendance on: what should it ask them?
    void punchContextRequested(const QString &serial, int requestId, const QString &userPin,
                               const QDateTime &punchTime);

private:
    struct Session {
        QTcpSocket *socket = nullptr;
        QByteArray  buffer;            // raw bytes not yet parsed into frames
        QByteArray  message;           // fragments of a message still arriving
        QString     serial;
        QString     ip;
        bool        json = true;       // the device's data format, learned from its messages
        bool        greeted = false;
        qint64      lastRxMs = 0;
        qint64      lastPingMs = 0;
        // command delivered and not yet answered
        int         commandId = 0;
        QString     commandName;
        qint64      commandSentMs = 0;
        // templates arriving for a backup
        int         templatesFor = 0;
        QList<BiometricTemplate> templates;
        // punches waiting to be stored, and the ACKs to send once they are
        QList<AttendanceRecord> punches;
        QList<qint64>           punchIds;
        // text format: an ANSWER line arrives just before its PUNCH line
        QHash<qint64, AttendanceRecord> answers;
    };

    void onReadyRead(QTcpSocket *socket);
    void onDisconnected(QTcpSocket *socket);
    void processFrames(Session &s);
    void onMessage(Session &s, const QByteArray &payload);
    void onJsonMessage(Session &s, const QJsonObject &obj, const QByteArray &raw);
    void onTextMessage(Session &s, const QString &line);

    void onHello(Session &s, const QString &serial, const QString &summary);
    void onPunch(Session &s, qint64 id, const QString &userId, const QString &name, const QString &time,
                 const QString &raw, const AttendanceRecord *answer);
    void onContextRequest(Session &s, int requestId, const QString &userId, const QString &time);
    void onTemplate(Session &s, int commandId, const QString &userId, int finger, int size,
                    const QString &data);
    void onResult(Session &s, int commandId, int returnCode, int count, const QString &message);

    void flushPunches();
    void tick();
    void deliverCommand(Session &s, const QueuedCommand &cmd);
    void sendText(Session &s, const QString &text);
    void sendFrame(QTcpSocket *socket, quint8 opcode, const QByteArray &payload);
    QString label(const Session &s) const;

    QHash<QTcpSocket*, Session> m_sessions;
    CommandProvider             m_commandProvider;
    QTimer                     *m_tick  = nullptr;
    QTimer                     *m_flush = nullptr;
};
