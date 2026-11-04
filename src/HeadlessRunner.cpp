#include "HeadlessRunner.h"
#include "AppPaths.h"
#include <QTimer>
#include <QDateTime>
#include <QTextStream>

static constexpr int RETRY_MS = 3000;

HeadlessRunner::HeadlessRunner(QObject *parent) : QObject(parent) {
    m_db = new Database(this);
}

void HeadlessRunner::log(const QString &msg) {
    QTextStream out(stdout);
    out << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "  " << msg << "\n";
    out.flush();
}

bool HeadlessRunner::start() {
    const QString path = AppPaths::dbPath();
    if (!m_db->open(path)) {
        log(QStringLiteral("FATAL: could not open database at %1").arg(path));
        return false;
    }
    log(QStringLiteral("BioSync background service starting. DB: %1").arg(path));

    m_engine = new Engine(m_db, Engine::Role::Service, this);
    connect(m_engine, &Engine::logMessage, this, [this](const QString &serial, const QString &msg) {
        log(serial.isEmpty() ? msg : QStringLiteral("[%1] %2").arg(serial, msg));
    });

    // Retry loop: (re)acquire the port whenever we don't hold it — at startup, or if a port
    // change / restart ever loses it. The service never gives up and never exits for this.
    m_retry = new QTimer(this);
    m_retry->setInterval(RETRY_MS);
    connect(m_retry, &QTimer::timeout, this, [this]() { if (!m_engine->isRunning()) tryStart(); });
    m_retry->start();

    tryStart();
    return true;
}

void HeadlessRunner::tryStart() {
    const QString port = m_db->getSetting("server_port", "8085");

    if (m_engine->start()) {
        m_db->setSetting("service_waiting", "0");
        log(m_waiting ? QStringLiteral("Took over the attendance server on port %1.").arg(port)
                      : QStringLiteral("Attendance server listening on port %1.").arg(port));
        m_waiting = false;
        m_conflictLogged = false;
        const QString apiUrl = m_db->getSetting("api_url");
        if (apiUrl.isEmpty() || m_db->getSetting("api_key").isEmpty()
            || m_db->getSetting("institution_id", "0").toLongLong() <= 0)
            log(QStringLiteral("Waiting for API config (server URL / key / institution) — set it in the BioSync window."));
        return;
    }

    if (!m_engine->lastErrorWasAddressInUse()) {
        if (!m_conflictLogged)
            log(QStringLiteral("Could not open port %1: %2 — retrying every %3s.")
                    .arg(port, m_engine->lastErrorString()).arg(RETRY_MS / 1000));
        m_conflictLogged = true;
        return;
    }

    // Port busy. If another BioSync *service* already has it, there's nothing to take over.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool otherServiceAlive =
        m_db->getSetting("engine_role") == "service" &&
        now - m_db->getSetting("engine_heartbeat", "0").toLongLong() < 3 * Engine::HEARTBEAT_MS;
    if (otherServiceAlive) {
        if (!m_waiting)
            log(QStringLiteral("Another BioSync service already runs the attendance server — standing by."));
        m_waiting = true;
        return;
    }

    // Most likely the BioSync window holds it: ask it to hand over (it checks every few seconds).
    m_db->setSetting("service_waiting", QString::number(now));
    if (!m_waiting)
        log(QStringLiteral("Port %1 is held by the BioSync window — asking it to hand over to the service…").arg(port));
    m_waiting = true;
}
