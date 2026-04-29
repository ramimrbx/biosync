#include "ZkAdmsServer.h"
#include <QDebug>
#include <QUrl>
#include <QUrlQuery>
#include <QSet>

ZkAdmsServer::ZkAdmsServer(QObject *parent) : QObject(parent) {}

void ZkAdmsServer::setKnownSerials(const QSet<QString> &serials) {
    m_knownSerials = serials;
}

void ZkAdmsServer::addKnownSerial(const QString &serial) {
    m_knownSerials.insert(serial);
}

bool ZkAdmsServer::start(quint16 port) {
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &ZkAdmsServer::onNewConnection);

    if (!m_server->listen(QHostAddress::AnyIPv4, port)) {
        emit logMessage({}, QString("Failed to start server on port %1: %2")
                        .arg(port).arg(m_server->errorString()));
        return false;
    }

    emit logMessage({}, QString("ZKTeco ADMS server started on port %1").arg(port));
    return true;
}

void ZkAdmsServer::stop() {
    if (m_server) {
        m_server->close();
        emit logMessage({}, "ZKTeco ADMS server stopped.");
    }
}

bool ZkAdmsServer::isRunning() const {
    return m_server && m_server->isListening();
}

quint16 ZkAdmsServer::port() const {
    return m_server ? m_server->serverPort() : 0;
}

// ── Command queue ─────────────────────────────────────────────────────────────

void ZkAdmsServer::queueCommand(const QString &serial, const QString &command) {
    m_commandQueues[serial].enqueue({command, {}});
    emit logMessage(serial, QString("Command queued for SN=%1: %2").arg(serial, command));
}

void ZkAdmsServer::queueBiometricQuery(const QString &serial) {
    m_commandQueues[serial].enqueue({"DATA QUERY FINGERTMP", {}});
    emit logMessage(serial, QString("Biometric query queued for SN=%1 — device will upload templates on next poll.").arg(serial));
}

void ZkAdmsServer::queueBiometricRestore(const QString &serial,
                                          const QList<BiometricTemplate> &templates)
{
    if (templates.isEmpty()) {
        emit logMessage(serial, QString("Restore skipped for SN=%1 — no templates found.").arg(serial));
        return;
    }

    // Build the DATA UPDATE body: Pin=x\tFId=y\tTmp=z\tSize=n\tValid=1
    QStringList lines;
    for (const auto &t : templates) {
        lines << QString("Pin=%1\tFId=%2\tTmp=%3\tSize=%4\tValid=%5")
                     .arg(t.userPin)
                     .arg(t.fingerIndex)
                     .arg(t.templateData)
                     .arg(t.templateSize)
                     .arg(t.valid ? 1 : 0);
    }

    m_commandQueues[serial].enqueue({"DATA UPDATE FINGERTMP", lines.join("\n")});
    emit logMessage(serial, QString("Biometric restore queued for SN=%1 — %2 template(s) will be pushed on next poll.")
                    .arg(serial).arg(templates.size()));
}

// ── TCP server ────────────────────────────────────────────────────────────────

void ZkAdmsServer::onNewConnection() {
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, &ZkAdmsServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &ZkAdmsServer::onSocketDisconnected);
        m_buffers[socket] = QByteArray();
    }
}

void ZkAdmsServer::onReadyRead() {
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    m_buffers[socket].append(socket->readAll());
    QByteArray &buf = m_buffers[socket];

    int headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0) return;

    QString headers = QString::fromLatin1(buf.left(headerEnd));
    int contentLength = 0;
    for (const QString &line : headers.split("\r\n")) {
        if (line.toLower().startsWith("content-length:")) {
            contentLength = line.mid(15).trimmed().toInt();
            break;
        }
    }

    int bodyStart = headerEnd + 4;
    if (buf.size() < bodyStart + contentLength) return;

    ParsedRequest req = parseHttpRequest(buf.left(bodyStart + contentLength));
    if (req.valid) handleRequest(socket, req);

    m_buffers[socket].clear();
    socket->disconnectFromHost();
}

void ZkAdmsServer::onSocketDisconnected() {
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (socket) {
        m_buffers.remove(socket);
        socket->deleteLater();
    }
}

// ── Request parsing ───────────────────────────────────────────────────────────

ParsedRequest ZkAdmsServer::parseHttpRequest(const QByteArray &data) {
    ParsedRequest req;
    int headerEnd = data.indexOf("\r\n\r\n");
    if (headerEnd < 0) return req;

    QStringList headerLines = QString::fromLatin1(data.left(headerEnd)).split("\r\n");
    if (headerLines.isEmpty()) return req;

    QStringList requestLine = headerLines[0].split(' ');
    if (requestLine.size() < 2) return req;

    req.method = requestLine[0].toUpper();
    QString fullPath = requestLine.value(1);

    int qmark = fullPath.indexOf('?');
    if (qmark >= 0) {
        req.path = fullPath.left(qmark);
        QUrlQuery query(fullPath.mid(qmark + 1));
        for (auto &pair : query.queryItems())
            req.queryParams[pair.first] = pair.second;
    } else {
        req.path = fullPath;
    }

    for (int i = 1; i < headerLines.size(); ++i) {
        int colon = headerLines[i].indexOf(':');
        if (colon > 0) {
            req.headers[headerLines[i].left(colon).trimmed().toLower()] =
                headerLines[i].mid(colon + 1).trimmed();
        }
    }

    req.body  = data.mid(headerEnd + 4);
    req.valid = true;
    return req;
}

// ── Request dispatch ──────────────────────────────────────────────────────────

void ZkAdmsServer::handleRequest(QTcpSocket *socket, const ParsedRequest &req) {
    QString serial = queryParam(req, "SN");
    QString ip     = socket->peerAddress().toString();

    if (!serial.isEmpty()) {
        if (!m_knownSerials.contains(serial))
            emit unknownDeviceConnected(serial, ip);
        emit deviceHeartbeat(serial, ip);
    }

    if (req.method == "GET" && req.path.startsWith("/iclock/getrequest")) {
        handleGetRequest(socket, req);
    } else if (req.method == "POST" && req.path.startsWith("/iclock/cdata")) {
        handleAttendancePost(socket, req);
    } else if (req.method == "POST" && req.path.startsWith("/iclock/deviceinfo")) {
        handleDeviceInfo(socket, req);
    } else {
        // Log unknown paths — helps identify device-specific endpoints
        emit logMessage(serial, QString("SN=%1: %2 %3 (%4 bytes) — unhandled, responding OK.")
                        .arg(serial, req.method, req.path).arg(req.body.size()));
        sendResponse(socket, 200, "OK");
    }
}

void ZkAdmsServer::handleGetRequest(QTcpSocket *socket, const ParsedRequest &req) {
    QString serial = queryParam(req, "SN");
    emit logMessage(serial, QString("Heartbeat from device SN=%1").arg(serial));

    // Deliver next pending command if any
    if (m_commandQueues.contains(serial) && !m_commandQueues[serial].isEmpty()) {
        QueuedCommand cmd = m_commandQueues[serial].dequeue();
        QString responseBody = QString("C:%1:%2").arg(m_commandId++).arg(cmd.command);
        if (!cmd.body.isEmpty())
            responseBody += "\n" + cmd.body;
        emit logMessage(serial, QString("Delivering command to SN=%1: %2").arg(serial, cmd.command));
        sendResponse(socket, 200, responseBody);
        return;
    }

    sendResponse(socket, 200, "OK");
}

// Table names different ZKTeco models use for fingerprint templates
static bool isFingerprintTable(const QString &table) {
    return table == "FINGERTMP"
        || table == "FINGERTMPE"
        || table == "FPTEMPLATE"
        || table == "FPTMP"
        || table == "BIODATA"
        || table == "BIOTEMPLATE"
        || table == "USERFINGERTEMP"
        || table.contains("FINGER")
        || table.contains("FPTMP")
        || table.contains("BIOMETRIC");
}

void ZkAdmsServer::handleAttendancePost(QTcpSocket *socket, const ParsedRequest &req) {
    QString serial = queryParam(req, "SN");
    QString table  = queryParam(req, "table").trimmed().toUpper();

    // Always log every incoming POST so unknown tables are visible
    emit logMessage(serial, QString("POST from SN=%1 table=%2 body_bytes=%3")
                    .arg(serial, table.isEmpty() ? "(none)" : table)
                    .arg(req.body.size()));

    if (table == "ATTLOG") {
        QList<AttendanceRecord> records = parseAttendanceData(serial, req.body);
        emit logMessage(serial, QString("Parsed %1 attendance record(s) from SN=%2")
                        .arg(records.size()).arg(serial));
        if (!records.isEmpty())
            emit attendanceReceived(records);
        sendResponse(socket, 200, QString("OK: %1").arg(records.size()));

    } else if (isFingerprintTable(table)) {
        // Device responding to our DATA QUERY FINGERTMP command
        if (req.body.isEmpty()) {
            emit logMessage(serial, QString("SN=%1: fingerprint POST received but body is empty "
                                           "(device may not support ADMS template sync).").arg(serial));
            sendResponse(socket, 200, "OK: 0");
            return;
        }
        QList<BiometricTemplate> templates = parseFingerprintData(req.body);
        emit logMessage(serial, QString("Parsed %1 biometric template(s) from SN=%2 (table=%3)")
                        .arg(templates.size()).arg(serial, table));
        if (!templates.isEmpty())
            emit biometricTemplatesReceived(serial, templates);
        sendResponse(socket, 200, QString("OK: %1").arg(templates.size()));

    } else if (table == "OPERLOG") {
        // Operation log — acknowledge, currently not processed
        emit logMessage(serial, QString("SN=%1: OPERLOG received (%2 bytes) — acknowledged.")
                        .arg(serial).arg(req.body.size()));
        sendResponse(socket, 200, "OK");

    } else if (table.isEmpty()) {
        // Some devices POST without a table parameter — log body preview for debugging
        QString preview = QString::fromUtf8(req.body.left(256)).trimmed();
        emit logMessage(serial, QString("SN=%1: POST with no table parameter. Body preview: %2")
                        .arg(serial, preview.left(200)));
        sendResponse(socket, 200, "OK");

    } else {
        // Unknown table — log body preview so we can identify what the device is sending
        QString preview = QString::fromUtf8(req.body.left(256)).trimmed();
        emit logMessage(serial, QString("SN=%1: Unknown table '%2'. Body preview: %3")
                        .arg(serial, table, preview.left(200)));
        sendResponse(socket, 200, "OK");
    }
}

void ZkAdmsServer::handleDeviceInfo(QTcpSocket *socket, const ParsedRequest &req) {
    QString serial = queryParam(req, "SN");
    QString body   = QString::fromLatin1(req.body).trimmed();
    emit logMessage(serial, QString("Device info from SN=%1: %2").arg(serial, body.left(200)));
    sendResponse(socket, 200, "OK");
}

// ── Data parsers ──────────────────────────────────────────────────────────────

QList<AttendanceRecord> ZkAdmsServer::parseAttendanceData(const QString &serial,
                                                           const QByteArray &body) {
    QList<AttendanceRecord> records;
    for (const QString &rawLine : QString::fromUtf8(body).trimmed().split('\n')) {
        QString line = rawLine.trimmed();
        if (line.isEmpty()) continue;

        QStringList fields = line.split('\t');
        if (fields.size() < 2) continue;

        QString pin     = fields[0].trimmed();
        QString timeStr = fields[1].trimmed();
        if (pin.isEmpty() || timeStr.isEmpty()) continue;

        QDateTime dt = QDateTime::fromString(timeStr, "yyyy-MM-dd HH:mm:ss");
        if (!dt.isValid()) dt = QDateTime::fromString(timeStr, "yyyy/MM/dd HH:mm:ss");
        if (!dt.isValid()) continue;

        AttendanceRecord r;
        r.deviceSerial = serial;
        r.userPin      = pin;
        r.punchTime    = dt;
        r.punchStatus  = (fields.size() > 2) ? fields[2].trimmed().toInt() : 0;
        r.verifyType   = (fields.size() > 3) ? fields[3].trimmed().toInt() : 0;
        r.workCode     = (fields.size() > 4) ? fields[4].trimmed() : QString();
        r.rawData      = line;
        records.append(r);
    }
    return records;
}

QList<BiometricTemplate> ZkAdmsServer::parseFingerprintData(const QByteArray &body) {
    // ZKTeco FINGERTMP POST format (tab-separated per line):
    //   PIN  FID  FPTEMP(base64)  SIZE  VALID
    QList<BiometricTemplate> templates;
    for (const QString &rawLine : QString::fromUtf8(body).trimmed().split('\n')) {
        QString line = rawLine.trimmed();
        if (line.isEmpty()) continue;

        QStringList fields = line.split('\t');
        if (fields.size() < 3) continue;

        BiometricTemplate t;
        t.userPin      = fields[0].trimmed();
        t.fingerIndex  = fields[1].trimmed().toInt();
        t.templateData = fields[2].trimmed();
        t.templateSize = (fields.size() > 3) ? fields[3].trimmed().toInt() : 0;
        t.valid        = (fields.size() > 4) ? (fields[4].trimmed() != "0") : true;

        if (!t.userPin.isEmpty() && !t.templateData.isEmpty())
            templates.append(t);
    }
    return templates;
}

// ── HTTP response ─────────────────────────────────────────────────────────────

void ZkAdmsServer::sendResponse(QTcpSocket *socket, int status, const QString &body) {
    QByteArray bodyBytes = body.toUtf8();
    QString response = QString("HTTP/1.1 %1 OK\r\n"
                               "Content-Type: text/plain\r\n"
                               "Content-Length: %2\r\n"
                               "Connection: close\r\n"
                               "\r\n")
                           .arg(status)
                           .arg(bodyBytes.size());
    socket->write(response.toLatin1());
    socket->write(bodyBytes);
    socket->flush();
}

QString ZkAdmsServer::queryParam(const ParsedRequest &req, const QString &key) const {
    return req.queryParams.value(key);
}
