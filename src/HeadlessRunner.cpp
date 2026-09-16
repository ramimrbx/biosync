#include "HeadlessRunner.h"
#include "AppPaths.h"
#include <QTimer>
#include <QDateTime>
#include <QTextStream>

HeadlessRunner::HeadlessRunner(QObject *parent) : QObject(parent) {
    m_db     = new Database(this);
    m_server = new ZkAdmsServer(this);
    m_pusher = new ApiPusher(m_db, this);
}

void HeadlessRunner::log(const QString &msg) {
    QTextStream(stdout) << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
                        << "  " << msg << "\n";
    QTextStream(stdout).flush();
}

bool HeadlessRunner::start() {
    const QString path = AppPaths::dbPath();
    if (!m_db->open(path)) {
        log(QStringLiteral("FATAL: could not open database at %1").arg(path));
        return false;
    }
    log(QStringLiteral("BioSync headless service starting. DB: %1").arg(path));

    connect(m_server, &ZkAdmsServer::attendanceReceived,     this, &HeadlessRunner::onAttendanceReceived);
    connect(m_server, &ZkAdmsServer::unknownDeviceConnected, this, &HeadlessRunner::onUnknownDeviceConnected);
    connect(m_server, &ZkAdmsServer::deviceHeartbeat,        this, &HeadlessRunner::onDeviceHeartbeat);
    connect(m_server, &ZkAdmsServer::logMessage,             this, &HeadlessRunner::onLog);
    connect(m_pusher, &ApiPusher::logMessage,                this, [this](const QString &msg){ log(msg); });

    syncKnownSerials();

    const quint16 port = static_cast<quint16>(m_db->getSetting("server_port", "8085").toUShort());
    if (!m_server->start(port)) {
        log(QStringLiteral("FATAL: could not bind ADMS server on port %1").arg(port));
        return false;
    }
    log(QStringLiteral("ADMS server listening on port %1").arg(port));

    reloadPusherConfig();
    // The config may be set/changed later in the GUI (which writes the same shared DB); pick it up.
    m_configTimer = new QTimer(this);
    m_configTimer->setInterval(30000);
    connect(m_configTimer, &QTimer::timeout, this, [this] { syncKnownSerials(); reloadPusherConfig(); });
    m_configTimer->start();
    return true;
}

void HeadlessRunner::reloadPusherConfig() {
    const QString apiUrl = m_db->getSetting("api_url");
    const QString apiKey = m_db->getSetting("api_key");
    const long long instId = m_db->getSetting("institution_id", "0").toLongLong();
    m_pusher->setConfig(apiUrl, apiKey, instId);
    if (apiUrl.isEmpty() || apiKey.isEmpty() || instId <= 0)
        log(QStringLiteral("Waiting for API config (server URL / key / institution) — set it in the BioSync GUI."));
}

void HeadlessRunner::syncKnownSerials() {
    QSet<QString> serials;
    for (const Device &d : m_db->getAllDevices())
        serials.insert(d.serialNumber);
    m_server->setKnownSerials(serials);
}

void HeadlessRunner::onAttendanceReceived(const QList<AttendanceRecord> &records) {
    int stored = 0;
    for (auto r : records) {
        Device d = m_db->getDeviceBySerial(r.deviceSerial);
        if (d.id && !d.isActive) continue;   // skip deactivated devices
        if (!d.name.isEmpty()) r.deviceName = d.name;
        if (m_db->addAttendanceRecord(r) > 0) stored++;
    }
    if (stored > 0) log(QStringLiteral("Stored %1 punch(es); pending %2").arg(stored).arg(m_db->getPendingCount()));
    m_pusher->pushNow();
}

void HeadlessRunner::onUnknownDeviceConnected(const QString &serial, const QString &ip) {
    // No user to prompt in headless mode — auto-register the device so its punches start counting.
    Device d;
    d.name = QStringLiteral("Device %1").arg(serial);
    d.serialNumber = serial;
    d.ipAddress = ip;
    d.port = 4370;
    d.isActive = true;
    if (m_db->addDevice(d) > 0) {
        m_server->addKnownSerial(serial);
        log(QStringLiteral("Auto-registered new device %1 (%2)").arg(serial, ip));
    }
}

void HeadlessRunner::onDeviceHeartbeat(const QString &serial, const QString &ip) {
    m_db->appendLog(serial, QStringLiteral("Heartbeat from %1").arg(ip));
}

void HeadlessRunner::onLog(const QString &serial, const QString &message) {
    log(serial.isEmpty() ? message : QStringLiteral("[%1] %2").arg(serial, message));
}
