#include "RitosLink.h"
#include <QTcpSocket>
#include <QTimer>
#include <QDateTime>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

namespace {
constexpr quint8  OP_CONTINUATION = 0x0;
constexpr quint8  OP_TEXT         = 0x1;
constexpr quint8  OP_BINARY       = 0x2;
constexpr quint8  OP_CLOSE        = 0x8;
constexpr quint8  OP_PING         = 0x9;
constexpr quint8  OP_PONG         = 0xA;

constexpr qint64  MAX_MESSAGE_BYTES   = 4 * 1024 * 1024;
constexpr qint64  PING_INTERVAL_MS    = 30000;
constexpr qint64  IDLE_TIMEOUT_MS     = 75000;     // device pings every 15 s
constexpr qint64  COMMAND_TIMEOUT_MS  = 10 * 60 * 1000;
constexpr int     PUNCH_FLUSH_MS      = 150;       // gathers a backlog into one database batch
// RitOS accepts WebSocket messages up to 15 KB, so a restore body goes in parts below that.
constexpr int     MAX_BODY_PART_BYTES = 10 * 1024;

const char *WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

QString normalizedCommand(const QString &command) { return command.trimmed().toUpper(); }

QDateTime parsePunchTime(const QString &text) {
    QDateTime t = QDateTime::fromString(text, Qt::ISODate);
    if (!t.isValid()) t = QDateTime::fromString(text, "yyyy-MM-dd HH:mm:ss");
    return t;
}

// Splits a restore body into parts of whole lines, each under the device's message limit.
QStringList splitBody(const QString &body) {
    QStringList parts;
    QString current;
    for (const QString &line : body.split('\n', Qt::SkipEmptyParts)) {
        if (!current.isEmpty() && current.size() + line.size() + 1 > MAX_BODY_PART_BYTES) {
            parts << current;
            current.clear();
        }
        if (!current.isEmpty()) current += '\n';
        current += line;
    }
    if (!current.isEmpty() || parts.isEmpty()) parts << current;
    return parts;
}
}

RitosLink::RitosLink(QObject *parent) : QObject(parent) {
    m_tick = new QTimer(this);
    m_tick->setInterval(1000);
    connect(m_tick, &QTimer::timeout, this, &RitosLink::tick);
    m_tick->start();

    m_flush = new QTimer(this);
    m_flush->setSingleShot(true);
    m_flush->setInterval(PUNCH_FLUSH_MS);
    connect(m_flush, &QTimer::timeout, this, &RitosLink::flushPunches);
}

bool RitosLink::isUpgradeRequest(const ParsedRequest &req) {
    return req.method == "GET"
        && req.headers.value("upgrade").compare("websocket", Qt::CaseInsensitive) == 0
        && !req.headers.value("sec-websocket-key").isEmpty();
}

// ── Connection ────────────────────────────────────────────────────────────────

void RitosLink::adopt(QTcpSocket *socket, const ParsedRequest &req, const QByteArray &leftover) {
    const QByteArray key = req.headers.value("sec-websocket-key").toLatin1();
    const QByteArray accept =
        QCryptographicHash::hash(key + WS_GUID, QCryptographicHash::Sha1).toBase64();

    QByteArray response = "HTTP/1.1 101 Switching Protocols\r\n"
                          "Upgrade: websocket\r\n"
                          "Connection: Upgrade\r\n"
                          "Sec-WebSocket-Accept: " + accept + "\r\n";
    // Echo the first sub-protocol the client offers (arduinoWebSockets sends "arduino").
    const QString protocols = req.headers.value("sec-websocket-protocol");
    if (!protocols.isEmpty())
        response += "Sec-WebSocket-Protocol: " + protocols.split(',').first().trimmed().toLatin1() + "\r\n";
    response += "\r\n";
    socket->write(response);
    socket->flush();

    Session s;
    s.socket   = socket;
    s.ip       = socket->peerAddress().toString();
    if (s.ip.startsWith("::ffff:")) s.ip = s.ip.mid(7);
    s.serial   = req.queryParams.value("SN").trimmed();
    s.lastRxMs = nowMs();
    s.buffer   = leftover;
    m_sessions.insert(socket, s);

    connect(socket, &QTcpSocket::readyRead,    this, [this, socket]() { onReadyRead(socket); });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() { onDisconnected(socket); });

    emit logMessage(s.serial, QString("RitOS device %1 connected over WebSocket from %2 (path %3).")
                                  .arg(s.serial.isEmpty() ? "(serial not sent yet)" : s.serial, s.ip, req.path));
    if (!s.serial.isEmpty()) emit deviceSeen(s.serial, s.ip);

    if (!leftover.isEmpty()) onReadyRead(socket);
}

void RitosLink::closeAll() {
    const auto sockets = m_sessions.keys();
    for (QTcpSocket *socket : sockets) {
        sendFrame(socket, OP_CLOSE, QByteArray::fromHex("03e9"));   // 1001: going away
        socket->flush();
        socket->abort();
    }
}

void RitosLink::onDisconnected(QTcpSocket *socket) {
    auto it = m_sessions.find(socket);
    if (it == m_sessions.end()) return;
    Session s = it.value();
    m_sessions.erase(it);
    socket->deleteLater();

    flushPunches();   // nothing to ACK any more, but the punches themselves are kept
    if (!s.punches.isEmpty()) emit attendanceReceived(s.punches);

    emit logMessage(s.serial, QString("RitOS device %1 disconnected.").arg(label(s)));
    if (s.commandId) {
        if (s.commandName == "REBOOT") {
            emit commandResult(s.serial, s.commandId, 0, "Device restarted.");
        } else {
            emit commandResult(s.serial, s.commandId, -100,
                               "The device disconnected before finishing the command.");
        }
    }
}

void RitosLink::onReadyRead(QTcpSocket *socket) {
    auto it = m_sessions.find(socket);
    if (it == m_sessions.end()) return;
    it->buffer.append(socket->readAll());
    processFrames(*it);
}

// ── Frames (RFC 6455) ─────────────────────────────────────────────────────────

void RitosLink::processFrames(Session &s) {
    QTcpSocket *socket = s.socket;
    bool closing = false;

    while (!closing) {
        const QByteArray &buf = s.buffer;
        if (buf.size() < 2) break;
        const quint8 b0 = quint8(buf[0]);
        const quint8 b1 = quint8(buf[1]);
        const bool   fin    = b0 & 0x80;
        const quint8 opcode = b0 & 0x0F;
        const bool   masked = b1 & 0x80;
        qint64 length = b1 & 0x7F;
        int pos = 2;

        if (length == 126) {
            if (buf.size() < 4) break;
            length = qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(buf.constData() + 2));
            pos = 4;
        } else if (length == 127) {
            if (buf.size() < 10) break;
            length = qint64(qFromBigEndian<quint64>(reinterpret_cast<const uchar *>(buf.constData() + 2)));
            pos = 10;
        }
        if (length < 0 || length > MAX_MESSAGE_BYTES) {
            emit logMessage(s.serial, QString("RitOS device %1 sent an oversized frame; closing.").arg(label(s)));
            closing = true;
            break;
        }

        QByteArray mask;
        if (masked) {
            if (buf.size() < pos + 4) break;
            mask = buf.mid(pos, 4);
            pos += 4;
        }
        if (buf.size() < pos + length) break;

        QByteArray payload = buf.mid(pos, length);
        s.buffer.remove(0, pos + length);
        if (masked)
            for (qint64 i = 0; i < payload.size(); ++i) payload[i] = payload[i] ^ mask[i % 4];

        s.lastRxMs = nowMs();
        switch (opcode) {
        case OP_TEXT:
        case OP_BINARY:
            s.message = payload;
            if (fin) { onMessage(s, s.message); s.message.clear(); }
            break;
        case OP_CONTINUATION:
            s.message.append(payload);
            if (s.message.size() > MAX_MESSAGE_BYTES) { closing = true; break; }
            if (fin) { onMessage(s, s.message); s.message.clear(); }
            break;
        case OP_PING:
            sendFrame(socket, OP_PONG, payload);
            if (!s.serial.isEmpty()) emit deviceSeen(s.serial, s.ip);   // the device's heartbeat
            break;
        case OP_PONG:
            if (!s.serial.isEmpty()) emit deviceSeen(s.serial, s.ip);
            break;
        case OP_CLOSE:
            sendFrame(socket, OP_CLOSE, payload.left(2));
            closing = true;
            break;
        default:
            break;
        }
    }

    // Let the device close its end; it frees our port without a TIME_WAIT (see ZkAdmsServer).
    if (closing) {
        QTimer::singleShot(3000, socket, [socket]() {
            if (socket->state() != QAbstractSocket::UnconnectedState) socket->abort();
        });
    }
}

void RitosLink::sendFrame(QTcpSocket *socket, quint8 opcode, const QByteArray &payload) {
    QByteArray frame;
    frame.append(char(0x80 | opcode));
    const qint64 n = payload.size();
    if (n < 126) {
        frame.append(char(n));
    } else if (n <= 0xFFFF) {
        frame.append(char(126));
        frame.append(char((n >> 8) & 0xFF));
        frame.append(char(n & 0xFF));
    } else {
        frame.append(char(127));
        for (int shift = 56; shift >= 0; shift -= 8) frame.append(char((n >> shift) & 0xFF));
    }
    frame.append(payload);   // server frames are never masked
    socket->write(frame);
}

void RitosLink::sendText(Session &s, const QString &text) {
    sendFrame(s.socket, OP_TEXT, text.toUtf8());
}

QString RitosLink::label(const Session &s) const {
    return s.serial.isEmpty() ? s.ip : QString("%1 (%2)").arg(s.serial, s.ip);
}

// ── Messages ──────────────────────────────────────────────────────────────────

void RitosLink::onMessage(Session &s, const QByteArray &payload) {
    const QByteArray trimmed = payload.trimmed();
    if (trimmed.isEmpty()) return;

    if (trimmed.startsWith('{')) {
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            emit logMessage(s.serial, QString("RitOS device %1 sent invalid JSON: %2")
                                          .arg(label(s), QString::fromUtf8(trimmed.left(120))));
            return;
        }
        s.json = true;
        onJsonMessage(s, doc.object(), trimmed);
    } else {
        s.json = false;
        for (const QByteArray &line : trimmed.split('\n')) {
            const QString text = QString::fromUtf8(line).trimmed();
            if (!text.isEmpty()) onTextMessage(s, text);
        }
    }
}

void RitosLink::onJsonMessage(Session &s, const QJsonObject &obj, const QByteArray &raw) {
    const QString type = obj.value("type").toString();
    if (s.serial.isEmpty() && !obj.value("device").toString().isEmpty())
        s.serial = obj.value("device").toString();

    if (type == "hello" || type == "status") {
        const QString summary = QString("%1 %2, IP %3, %4 user(s), %5 finger(s), %6 punch(es) stored, sensor %7")
            .arg(obj.value("name").toString("RitOS"), obj.value("os").toString(),
                 obj.value("ip").toString(s.ip))
            .arg(obj.value("users").toInt()).arg(obj.value("fingers").toInt())
            .arg(obj.value("punches").toInt())
            .arg(obj.value("sensorOnline").toBool(true) ? "online" : "OFFLINE");
        if (type == "hello") onHello(s, s.serial, summary);
        else if (!s.serial.isEmpty()) emit deviceSeen(s.serial, s.ip);
    } else if (type == "punch") {
        AttendanceRecord answer;
        const QJsonObject a = obj.value("answer").toObject();
        if (!a.isEmpty()) {
            answer.answered        = true;
            answer.answerDirection = a.value("direction").toString().toUpper();
            answer.answerExitType  = a.value("exitType").toString().toUpper();
            answer.answerReason    = a.value("reason").toString().trimmed();
            answer.answerMistake   = a.value("mistake").toBool();
        }
        onPunch(s, obj.value("id").toVariant().toLongLong(), obj.value("userId").toString(),
                obj.value("name").toString(), obj.value("time").toString(), QString::fromUtf8(raw),
                answer.answered ? &answer : nullptr);
    } else if (type == "context") {
        onContextRequest(s, obj.value("id").toInt(), obj.value("userId").toString(), obj.value("time").toString());
    } else if (type == "template") {
        onTemplate(s, obj.value("command").toInt(), obj.value("userId").toString(),
                   obj.value("finger").toInt(), obj.value("size").toInt(), obj.value("data").toString());
    } else if (type == "result") {
        onResult(s, obj.value("id").toInt(), obj.value("return").toInt(), obj.value("count").toInt(),
                 obj.value("message").toString());
    } else {
        emit logMessage(s.serial, QString("RitOS device %1: unhandled message type '%2'.").arg(label(s), type));
    }
}

void RitosLink::onTextMessage(Session &s, const QString &line) {
    const QStringList f = line.split(',');
    const QString kind = f.value(0).trimmed().toUpper();

    if (kind == "HELLO" && f.size() >= 2) {
        // HELLO,<serial>,<name>,<os>,<ip>,<users>,<fingers>,<punches>,<sensor online>
        if (s.serial.isEmpty()) s.serial = f[1].trimmed();
        const QString summary = QString("%1 %2, IP %3, %4 user(s), %5 finger(s), %6 punch(es) stored, sensor %7")
            .arg(f.value(2, "RitOS"), f.value(3), f.value(4, s.ip), f.value(5, "?"), f.value(6, "?"),
                 f.value(7, "?"), f.value(8) == "0" ? "OFFLINE" : "online");
        onHello(s, s.serial, summary);
    } else if (kind == "PUNCH" && f.size() >= 5) {
        // PUNCH,<id>,<userId>,<name — may itself contain commas>,<time>
        const qint64 id = f[1].trimmed().toLongLong();
        const AttendanceRecord answer = s.answers.take(id);
        onPunch(s, id, f[2].trimmed(), f.mid(3, f.size() - 4).join(',').trimmed(), f.last().trimmed(), line,
                answer.answered ? &answer : nullptr);
    } else if (kind == "ANSWER" && f.size() >= 5) {
        // ANSWER,<punch id>,<ENTRY|EXIT>,<exit type or ->,<mistake 0/1>,<reason — may contain commas>
        AttendanceRecord answer;
        answer.answered        = true;
        answer.answerDirection = f[2].trimmed().toUpper();
        answer.answerExitType  = f[3].trimmed() == "-" ? QString() : f[3].trimmed().toUpper();
        answer.answerMistake   = f[4].trimmed() == "1";
        answer.answerReason    = f.mid(5).join(',').trimmed();
        s.answers.insert(f[1].trimmed().toLongLong(), answer);
    } else if (kind == "CONTEXT" && f.size() >= 4) {
        // CONTEXT,<request id>,<userId>,<time>
        onContextRequest(s, f[1].toInt(), f[2].trimmed(), f[3].trimmed());
    } else if (kind == "TEMPLATE" && f.size() >= 6) {
        // TEMPLATE,<command id>,<userId>,<finger>,<size>,<base64>
        onTemplate(s, f[1].toInt(), f[2].trimmed(), f[3].toInt(), f[4].toInt(), f[5].trimmed());
    } else if (kind == "RESULT" && f.size() >= 4) {
        // RESULT,<id>,<return>,<count>,<command>,<message — may contain commas>
        onResult(s, f[1].toInt(), f[2].toInt(), f[3].toInt(), f.mid(5).join(',').trimmed());
    } else {
        emit logMessage(s.serial, QString("RitOS device %1: unhandled line '%2'.").arg(label(s), line.left(120)));
    }
}

void RitosLink::onHello(Session &s, const QString &serial, const QString &summary) {
    if (serial.isEmpty()) {
        emit logMessage({}, QString("RitOS device at %1 did not send its serial number; "
                                    "update its firmware.").arg(s.ip));
        return;
    }
    s.greeted = true;
    emit deviceSeen(serial, s.ip);
    emit logMessage(serial, QString("RitOS device %1 says hello (%2 format): %3")
                                .arg(serial, s.json ? "JSON" : "text", summary));
}

void RitosLink::onContextRequest(Session &s, int requestId, const QString &userId, const QString &time) {
    if (s.serial.isEmpty() || userId.isEmpty()) return;
    QDateTime t = parsePunchTime(time);
    if (!t.isValid()) t = QDateTime::currentDateTime();
    emit punchContextRequested(s.serial, requestId, userId, t);
}

void RitosLink::sendPunchContext(const QString &serial, int requestId, const QJsonObject &context) {
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        if (it->serial != serial) continue;
        QJsonObject reply = context;
        reply.insert("type", "context");
        reply.insert("id", requestId);
        // Always JSON: the device reads JSON from the server in either data format.
        sendText(*it, QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact)));
        return;
    }
}

void RitosLink::onPunch(Session &s, qint64 id, const QString &userId, const QString &name, const QString &time,
                        const QString &raw, const AttendanceRecord *answer) {
    if (s.serial.isEmpty()) {
        emit logMessage({}, QString("Punch from an unidentified RitOS device at %1 ignored.").arg(s.ip));
        return;
    }
    if (userId.isEmpty()) return;

    AttendanceRecord r;
    r.deviceSerial = s.serial;
    r.userPin      = userId;
    r.punchTime    = parsePunchTime(time);
    r.verifyType   = 1;   // fingerprint, the only way to punch on RitOS
    r.punchStatus  = 0;
    r.rawData      = raw;
    r.userName     = name == "Deleted user" ? QString() : name;
    if (answer) {
        r.answered        = true;
        r.answerDirection = answer->answerDirection;
        r.answerExitType  = answer->answerExitType;
        r.answerReason    = answer->answerReason;
        r.answerMistake   = answer->answerMistake;
        // Keep the device's direction visible in the BioSync table (0 = in, 1 = out).
        r.punchStatus     = answer->answerDirection == "EXIT" ? 1 : 0;
    }
    if (!r.punchTime.isValid()) {
        // The device clock was not set when the punch was taken.
        r.punchTime = QDateTime::currentDateTime();
        emit logMessage(s.serial, QString("Punch %1 from %2 has no time (device clock not set) — "
                                          "using the time it arrived.").arg(id).arg(s.serial));
    }

    s.punches.append(r);
    s.punchIds.append(id);
    if (!m_flush->isActive()) m_flush->start();
}

// Stores the gathered punches, then confirms each one so the device can count it as delivered.
void RitosLink::flushPunches() {
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        Session &s = it.value();
        if (s.punches.isEmpty()) continue;
        const QList<AttendanceRecord> punches = s.punches;
        const QList<qint64> ids = s.punchIds;
        s.punches.clear();
        s.punchIds.clear();

        emit logMessage(s.serial, QString("Received %1 punch(es) from RitOS device %2.")
                                      .arg(punches.size()).arg(s.serial));
        emit attendanceReceived(punches);   // stored synchronously by the engine

        for (qint64 id : ids) {
            if (s.json) {
                sendText(s, QString::fromUtf8(QJsonDocument(QJsonObject{{"type", "ack"}, {"id", id}})
                                                  .toJson(QJsonDocument::Compact)));
            } else {
                sendText(s, QString("ACK,%1").arg(id));
            }
        }
    }
}

void RitosLink::onTemplate(Session &s, int commandId, const QString &userId, int finger, int size,
                           const QString &data) {
    if (userId.isEmpty() || data.isEmpty()) return;
    if (s.templatesFor != commandId) {
        s.templatesFor = commandId;
        s.templates.clear();
    }
    BiometricTemplate t;
    t.userPin      = userId;
    t.fingerIndex  = finger;
    t.templateData = data;
    t.templateSize = size;
    t.valid        = true;
    s.templates.append(t);
}

void RitosLink::onResult(Session &s, int commandId, int returnCode, int count, const QString &message) {
    QString command = s.commandId == commandId ? s.commandName : QString();
    if (s.commandId == commandId) {
        s.commandId = 0;
        s.commandName.clear();
    }

    emit logMessage(s.serial, QString("RitOS device %1: command %2 %3 finished, return %4%5")
                                  .arg(s.serial).arg(commandId).arg(command).arg(returnCode)
                                  .arg(message.isEmpty() ? QString() : " — " + message));

    if (command == "DATA QUERY FINGERTMP" && returnCode == 0) {
        QList<BiometricTemplate> templates;
        if (s.templatesFor == commandId) templates = s.templates;
        s.templates.clear();
        s.templatesFor = 0;
        if (templates.isEmpty()) {
            emit biometricQueryEmpty(s.serial, commandId);
        } else {
            if (templates.size() != count)
                emit logMessage(s.serial, QString("RitOS device %1 announced %2 finger(s) but %3 arrived.")
                                              .arg(s.serial).arg(count).arg(templates.size()));
            emit biometricTemplatesReceived(s.serial, templates);
        }
    }
    emit commandResult(s.serial, commandId, returnCode, message);
}

// ── Commands and keep-alive ──────────────────────────────────────────────────

void RitosLink::tick() {
    const qint64 now = nowMs();
    QList<QTcpSocket *> dead;

    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        Session &s = it.value();
        if (now - s.lastRxMs > IDLE_TIMEOUT_MS) { dead << s.socket; continue; }
        if (now - s.lastPingMs > PING_INTERVAL_MS) {
            s.lastPingMs = now;
            sendFrame(s.socket, OP_PING, QByteArray("biosync"));
        }

        if (s.commandId && now - s.commandSentMs > COMMAND_TIMEOUT_MS) {
            emit commandResult(s.serial, s.commandId, -101, "The device did not answer the command in time.");
            s.commandId = 0;
            s.commandName.clear();
        }

        // One command at a time, the next as soon as the device has answered the last.
        QueuedCommand cmd;
        if (!s.commandId && s.greeted && !s.serial.isEmpty() && m_commandProvider
            && m_commandProvider(s.serial, cmd)) {
            deliverCommand(s, cmd);
        }
    }

    for (QTcpSocket *socket : dead) {
        emit logMessage(m_sessions.value(socket).serial,
                        QString("RitOS device %1 went silent; closing its connection.")
                            .arg(label(m_sessions.value(socket))));
        socket->abort();   // disconnected → onDisconnected cleans up
    }
}

void RitosLink::deliverCommand(Session &s, const QueuedCommand &cmd) {
    const QString name = normalizedCommand(cmd.command);
    s.commandId     = cmd.id;
    s.commandName   = name;
    s.commandSentMs = nowMs();
    if (name == "DATA QUERY FINGERTMP") {
        s.templatesFor = cmd.id;
        s.templates.clear();
    }

    const QStringList parts = cmd.body.isEmpty() ? QStringList{QString()} : splitBody(cmd.body);
    for (int i = 0; i < parts.size(); ++i) {
        const bool more = i + 1 < parts.size();
        if (s.json) {
            QJsonObject obj{{"type", "command"}, {"id", cmd.id}, {"command", name}};
            if (!parts[i].isEmpty()) obj.insert("body", parts[i]);
            if (more) obj.insert("more", true);
            sendText(s, QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
        } else {
            QString text = QString("C:%1:%2%3").arg(cmd.id).arg(name, more ? ":MORE" : "");
            if (!parts[i].isEmpty()) text += "\n" + parts[i];
            sendText(s, text);
        }
    }
    emit logMessage(s.serial, QString("Delivering command to RitOS device %1: %2%3")
                                  .arg(s.serial, name,
                                       parts.size() > 1 ? QString(" (in %1 parts)").arg(parts.size()) : QString()));
}
