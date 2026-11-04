#include "Engine.h"
#include "Version.h"
#include <QTimer>
#include <QDateTime>
#include <QSet>

static constexpr qint64 ONLINE_GAP_MS = 120000;   // a device silent this long counts as reconnecting

Engine::Engine(Database *db, Role role, QObject *parent)
    : QObject(parent), m_db(db), m_role(role)
{
    m_server = new ZkAdmsServer(this);
    m_pusher = new ApiPusher(m_db, this);
    m_pusher->setAutoPush(false);   // only while we own the server

    m_server->setIdentity(QString("BIOSYNC/%1/%2").arg(BioSync::version(), roleName()));
    m_server->setCommandProvider([this](const QString &serial, QueuedCommand &out) {
        DeviceCommand c;
        if (!m_db->takeNextCommand(serial, c)) return false;
        out.id = c.id;
        out.command = c.command;
        out.body = c.body;
        return true;
    });

    connect(m_server, &ZkAdmsServer::attendanceReceived,     this, &Engine::onAttendanceReceived);
    connect(m_server, &ZkAdmsServer::deviceHeartbeat,        this, &Engine::onDeviceHeartbeat);
    connect(m_server, &ZkAdmsServer::unknownDeviceConnected, this, &Engine::onUnknownDevice);
    connect(m_server, &ZkAdmsServer::biometricTemplatesReceived, this, &Engine::onTemplatesReceived);
    connect(m_server, &ZkAdmsServer::biometricQueryEmpty,    this, &Engine::onTemplateQueryEmpty);
    connect(m_server, &ZkAdmsServer::deviceUserNames,        this, &Engine::onDeviceUserNames);
    connect(m_server, &ZkAdmsServer::admsDevicePolled,       this, &Engine::onAdmsDevicePolled);
    connect(m_server, &ZkAdmsServer::punchContextRequested,  this, &Engine::onPunchContextRequested);
    connect(m_server, &ZkAdmsServer::commandResult,          this, &Engine::onCommandResult);
    connect(m_server, &ZkAdmsServer::logMessage,             this, &Engine::log);

    connect(m_pusher, &ApiPusher::logMessage, this, [this](const QString &msg) { log({}, msg); });
    connect(m_pusher, &ApiPusher::statusChanged, this, [this](bool online, int) {
        m_db->setSetting("api_online", online ? "1" : "0");
    });

    m_tick = new QTimer(this);
    m_tick->setInterval(HEARTBEAT_MS);
    connect(m_tick, &QTimer::timeout, this, &Engine::tick);
}

void Engine::log(const QString &serial, const QString &message) {
    m_db->appendLog(serial, message);
    emit logMessage(serial, message);
}

bool Engine::start() {
    if (isRunning()) return true;

    applyConfig();
    const quint16 port = static_cast<quint16>(m_db->getSetting("server_port", "8085").toUShort());
    if (!m_server->start(port)) return false;

    m_configRev   = m_db->getSetting("config_rev");
    m_restartSeen = m_db->getSetting("engine_restart_request");
    m_pusher->setAutoPush(true);
    writeStatus(true);
    m_tick->start();
    m_pusher->pushNow();
    emit runningChanged(true, port);
    return true;
}

void Engine::stop() {
    if (!isRunning()) return;
    m_tick->stop();
    m_pusher->setAutoPush(false);
    m_server->stop();
    writeStatus(false);
    emit runningChanged(false, 0);
}

bool Engine::isRunning() const { return m_server->isRunning(); }
quint16 Engine::port() const   { return m_server->port(); }

bool Engine::lastErrorWasAddressInUse() const {
    return m_server->lastError() == QAbstractSocket::AddressInUseError;
}
QString Engine::lastErrorString() const { return m_server->lastErrorString(); }

void Engine::writeStatus(bool alive) {
    m_db->setSetting("engine_role", roleName());
    m_db->setSetting("engine_port", QString::number(alive ? port() : 0));
    m_db->setSetting("engine_heartbeat",
                     alive ? QString::number(QDateTime::currentMSecsSinceEpoch()) : "0");
}

void Engine::applyConfig() {
    const QString apiUrl = m_db->getSetting("api_url");
    const QString apiKey = m_db->getSetting("api_key");
    const long long instId = m_db->getSetting("institution_id", "0").toLongLong();
    m_pusher->setConfig(apiUrl, apiKey, instId);

    QSet<QString> serials;
    for (const Device &d : m_db->getAllDevices()) serials.insert(d.serialNumber);
    m_server->setKnownSerials(serials);
}

void Engine::restartServer() {
    const quint16 newPort = static_cast<quint16>(m_db->getSetting("server_port", "8085").toUShort());
    log({}, QString("Restarting the attendance server on port %1…").arg(newPort));
    m_server->stop();
    if (!m_server->start(newPort)) {
        log({}, QString("Could not open port %1: %2").arg(newPort).arg(m_server->lastErrorString()));
        // Lost the port (e.g. changed to one that is taken). Report it; the owner of this engine
        // (HeadlessRunner / MainWindow) notices isRunning()==false and retries.
        m_tick->stop();
        m_pusher->setAutoPush(false);
        writeStatus(false);
        emit runningChanged(false, 0);
        return;
    }
    writeStatus(true);
    emit runningChanged(true, newPort);
}

void Engine::tick() {
    writeStatus(true);

    // Settings saved in the window → pick them up now.
    const QString rev = m_db->getSetting("config_rev");
    if (rev != m_configRev) {
        m_configRev = rev;
        applyConfig();
        log({}, "Configuration reloaded.");
    } else if (++m_ticks % 15 == 0) {
        applyConfig();   // every ~30s anyway (devices added in the window, etc.)
    }

    // Restart requested from the window, or the port setting changed.
    const QString restart = m_db->getSetting("engine_restart_request");
    const quint16 wantPort = static_cast<quint16>(m_db->getSetting("server_port", "8085").toUShort());
    if (restart != m_restartSeen || wantPort != port()) {
        m_restartSeen = restart;
        restartServer();
    }
}

// ── Device events ────────────────────────────────────────────────────────────

void Engine::onAttendanceReceived(const QList<AttendanceRecord> &records) {
    int stored = 0;
    for (auto r : records) {
        Device d = m_db->getDeviceBySerial(r.deviceSerial);
        if (d.id && !d.isActive) continue;   // skip deactivated devices
        if (!d.name.isEmpty()) r.deviceName = d.name;
        if (m_db->addAttendanceRecord(r) > 0) stored++;
    }
    if (stored > 0) {
        log({}, QString("Stored %1 new attendance record(s); %2 pending sync.")
                    .arg(stored).arg(m_db->getPendingCount()));
        emit attendanceStored(stored);
    }
    m_pusher->pushNow();
}

void Engine::onDeviceHeartbeat(const QString &serial, const QString &ip) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 prev = m_lastSeen.value(serial, 0);
    m_lastSeen[serial] = now;
    m_db->markDeviceSeen(serial, ip);
    if (now - prev > ONLINE_GAP_MS)
        log(serial, QString("Device came online from %1").arg(ip));
}

void Engine::onUnknownDevice(const QString &serial, const QString &ip) {
    // Added in the window since our last config load? Then it's not new.
    if (m_db->getDeviceBySerial(serial).id) {
        m_server->addKnownSerial(serial);
        return;
    }
    Device d;
    d.name = QString("Device %1").arg(serial);
    d.serialNumber = serial;
    d.ipAddress = ip;
    d.port = 4370;
    d.isActive = true;
    if (m_db->addDevice(d) > 0) {
        m_server->addKnownSerial(serial);
        log(serial, QString("New device detected SN=%1 from %2 — registered automatically.").arg(serial, ip));
        emit deviceRegistered(serial, ip);
    }
}

void Engine::onTemplatesReceived(const QString &serial, const QList<BiometricTemplate> &templates) {
    const int n = templates.size();
    log(serial, QString("Received %1 biometric template(s) from SN=%2 — uploading to server.").arg(n).arg(serial));

    // Tie the upload to the backup request the window queued, so its progress dialog can follow.
    const DeviceCommand cmd = m_db->latestCommand(serial, "DATA QUERY FINGERTMP", "sent");
    if (cmd.id) m_db->setCommandStatus(cmd.id, "uploading", QString::number(n));

    const Device d = m_db->getDeviceBySerial(serial);
    const int cmdId = cmd.id;
    m_pusher->pushBiometricTemplates(serial, d.name, templates, [this, cmdId, n](bool ok, QString msg) {
        if (cmdId) m_db->setCommandStatus(cmdId, ok ? "done" : "failed", ok ? QString::number(n) : msg);
    });
}

void Engine::onTemplateQueryEmpty(const QString &serial, int commandId) {
    const DeviceCommand cmd = m_db->getCommand(commandId);
    if (!cmd.id || cmd.status != "sent") return;
    m_db->setCommandStatus(cmd.id, "done", "0");
    log(serial, QString("SN=%1 has no fingerprint templates to back up.").arg(serial));
}

void Engine::onCommandResult(const QString &, int commandId, int returnCode, const QString &message) {
    const DeviceCommand cmd = m_db->getCommand(commandId);
    if (!cmd.id || cmd.status != "sent") return;   // unknown, or already uploading/finished
    if (returnCode != 0) {
        m_db->setCommandStatus(cmd.id, "failed",
                               message.isEmpty() ? QString("Device returned error %1.").arg(returnCode)
                                                 : QString("%1 (error %2)").arg(message).arg(returnCode));
    } else if (cmd.command == "DATA QUERY FINGERTMP") {
        m_db->setCommandStatus(cmd.id, "sent", "ack");   // templates still to come
    } else {
        m_db->setCommandStatus(cmd.id, "done");
    }
}

// ── Names and interactive attendance ─────────────────────────────────────────

void Engine::onDeviceUserNames(const QString &serial, const QHash<QString, QString> &names) {
    const int n = m_db->setDeviceUserNames(serial, names);
    log(serial, QString("Saved %1 user name(s) reported by SN=%2.").arg(n).arg(serial));
}

// A ZKTeco device lists its users only when asked; ask once per connection session so the
// attendance table can show names. New enrolments arrive by themselves in its OPERLOG.
void Engine::onAdmsDevicePolled(const QString &serial) {
    if (m_userListRequested.contains(serial)) return;
    m_userListRequested.insert(serial);
    m_db->queueDeviceCommand(serial, "DATA QUERY USERINFO");
}

// A person just punched on a RitOS device with interactive attendance on. Ask RiTEMS what the
// popup would show and hand it to the device, which asks the person on its own screen.
void Engine::onPunchContextRequested(const QString &serial, int requestId, const QString &userPin,
                                     const QDateTime &punchTime) {
    m_pusher->fetchPunchContext(serial, userPin, punchTime,
        [this, serial, requestId, userPin](bool ok, QJsonObject context, QString error) {
            if (!ok) {
                // The device records a plain punch; RiTEMS then asks in the browser / app as before.
                context = QJsonObject{{"interactive", false}, {"note", error}};
                log(serial, QString("Interactive question for PIN %1 unavailable: %2 — the device "
                                    "records a plain punch.").arg(userPin, error));
            }
            m_server->sendPunchContext(serial, requestId, context);
        });
}
