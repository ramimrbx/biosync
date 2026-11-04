#include "Database.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QDateTime>

Database::Database(QObject *parent) : QObject(parent) {}

Database::~Database() {
    close();
}

bool Database::open(const QString &path) {
    QDir().mkpath(QFileInfo(path).absolutePath());

    m_db = QSqlDatabase::addDatabase("QSQLITE", "biosync");
    m_db.setDatabaseName(path);
    // The background service and the window share this file; wait for the other's write lock
    // instead of failing immediately with "database is locked".
    m_db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");

    if (!m_db.open()) {
        qCritical() << "Failed to open database:" << m_db.lastError().text();
        return false;
    }

    QSqlQuery q(m_db);
    q.exec("PRAGMA journal_mode=WAL");
    q.exec("PRAGMA foreign_keys=ON");

    createTables();

#ifndef Q_OS_WIN
    // The root service and the desktop user's window both write this DB (and its WAL side files).
    // Make them writable by both; this is a no-op/failure for files we don't own, which is fine.
    const QFileDevice::Permissions rw = QFileDevice::ReadOwner | QFileDevice::WriteOwner |
        QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther | QFileDevice::WriteOther;
    for (const QString &f : {path, path + "-wal", path + "-shm"})
        if (QFile::exists(f)) QFile::setPermissions(f, rw);
#endif
    return true;
}

void Database::close() {
    if (m_db.isOpen()) m_db.close();
}

void Database::createTables() {
    QSqlQuery q(m_db);

    q.exec(R"(
        CREATE TABLE IF NOT EXISTS settings (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        )
    )");

    q.exec(R"(
        CREATE TABLE IF NOT EXISTS devices (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            serial_number TEXT NOT NULL UNIQUE,
            ip_address TEXT NOT NULL,
            port INTEGER NOT NULL DEFAULT 4370,
            location TEXT,
            notes TEXT,
            is_active INTEGER NOT NULL DEFAULT 1,
            created_at TEXT NOT NULL DEFAULT (datetime('now'))
        )
    )");

    q.exec(R"(
        CREATE TABLE IF NOT EXISTS attendance_records (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            device_serial TEXT NOT NULL,
            device_name TEXT,
            user_pin TEXT NOT NULL,
            punch_time TEXT NOT NULL,
            verify_type INTEGER NOT NULL DEFAULT 0,
            punch_status INTEGER NOT NULL DEFAULT 0,
            work_code TEXT,
            raw_data TEXT,
            sync_status TEXT NOT NULL DEFAULT 'pending',
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            synced_at TEXT,
            error_message TEXT,
            UNIQUE(device_serial, user_pin, punch_time)
        )
    )");

    q.exec(R"(
        CREATE INDEX IF NOT EXISTS idx_att_sync ON attendance_records(sync_status)
    )");

    q.exec(R"(
        CREATE TABLE IF NOT EXISTS device_logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            device_serial TEXT,
            message TEXT NOT NULL,
            created_at TEXT NOT NULL DEFAULT (datetime('now', 'localtime'))
        )
    )");

    q.exec(R"(
        CREATE INDEX IF NOT EXISTS idx_logs_serial ON device_logs(device_serial)
    )");

    q.exec(R"(
        CREATE TABLE IF NOT EXISTS device_status (
            serial TEXT PRIMARY KEY,
            ip TEXT,
            last_seen INTEGER NOT NULL
        )
    )");

    q.exec(R"(
        CREATE TABLE IF NOT EXISTS device_commands (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            device_serial TEXT NOT NULL,
            command TEXT NOT NULL,
            body TEXT,
            status TEXT NOT NULL DEFAULT 'pending',
            result TEXT,
            created_at TEXT NOT NULL DEFAULT (datetime('now', 'localtime')),
            updated_at TEXT
        )
    )");

    q.exec(R"(
        CREATE INDEX IF NOT EXISTS idx_cmd_serial ON device_commands(device_serial, status)
    )");

    if (q.lastError().isValid())
        qWarning() << "Table creation error:" << q.lastError().text();

    // Before 1.3.0 any failed push (even a brief network drop) marked records 'failed' and they
    // were never retried. Put them back in the queue once; RiTEMS de-duplicates, so it's safe.
    QSqlQuery m(m_db);
    if (m.exec("SELECT value FROM settings WHERE key='migrated_failed_requeue'") && !m.next()) {
        m.exec("UPDATE attendance_records SET sync_status='pending' WHERE sync_status='failed'");
        m.exec("INSERT OR REPLACE INTO settings(key, value) VALUES('migrated_failed_requeue', '1')");
    }
}

QString Database::getSetting(const QString &key, const QString &defaultValue) const {
    QSqlQuery q(m_db);
    q.prepare("SELECT value FROM settings WHERE key = ?");
    q.addBindValue(key);
    if (q.exec() && q.next()) return q.value(0).toString();
    return defaultValue;
}

void Database::setSetting(const QString &key, const QString &value) {
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO settings(key, value) VALUES(?, ?)");
    q.addBindValue(key);
    q.addBindValue(value);
    q.exec();
}

static Device rowToDevice(QSqlQuery &q) {
    Device d;
    d.id = q.value("id").toInt();
    d.name = q.value("name").toString();
    d.serialNumber = q.value("serial_number").toString();
    d.ipAddress = q.value("ip_address").toString();
    d.port = q.value("port").toInt();
    d.location = q.value("location").toString();
    d.notes = q.value("notes").toString();
    d.isActive = q.value("is_active").toBool();
    d.createdAt = QDateTime::fromString(q.value("created_at").toString(), Qt::ISODate);
    return d;
}

QList<Device> Database::getAllDevices() const {
    QList<Device> list;
    QSqlQuery q(m_db);
    q.exec("SELECT * FROM devices ORDER BY name");
    while (q.next()) list.append(rowToDevice(q));
    return list;
}

Device Database::getDevice(int id) const {
    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM devices WHERE id = ?");
    q.addBindValue(id);
    if (q.exec() && q.next()) return rowToDevice(q);
    return {};
}

Device Database::getDeviceBySerial(const QString &serial) const {
    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM devices WHERE serial_number = ?");
    q.addBindValue(serial);
    if (q.exec() && q.next()) return rowToDevice(q);
    return {};
}

int Database::addDevice(const Device &device) {
    QSqlQuery q(m_db);
    q.prepare(R"(
        INSERT INTO devices(name, serial_number, ip_address, port, location, notes, is_active)
        VALUES(?, ?, ?, ?, ?, ?, ?)
    )");
    q.addBindValue(device.name);
    q.addBindValue(device.serialNumber);
    q.addBindValue(device.ipAddress);
    q.addBindValue(device.port);
    q.addBindValue(device.location);
    q.addBindValue(device.notes);
    q.addBindValue(device.isActive ? 1 : 0);
    if (q.exec()) return q.lastInsertId().toInt();
    qWarning() << "addDevice error:" << q.lastError().text();
    return -1;
}

bool Database::updateDevice(const Device &device) {
    QSqlQuery q(m_db);
    q.prepare(R"(
        UPDATE devices SET name=?, serial_number=?, ip_address=?, port=?,
        location=?, notes=?, is_active=? WHERE id=?
    )");
    q.addBindValue(device.name);
    q.addBindValue(device.serialNumber);
    q.addBindValue(device.ipAddress);
    q.addBindValue(device.port);
    q.addBindValue(device.location);
    q.addBindValue(device.notes);
    q.addBindValue(device.isActive ? 1 : 0);
    q.addBindValue(device.id);
    return q.exec();
}

bool Database::removeDevice(int id) {
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM devices WHERE id = ?");
    q.addBindValue(id);
    return q.exec();
}

static AttendanceRecord rowToRecord(QSqlQuery &q) {
    AttendanceRecord r;
    r.id = q.value("id").toInt();
    r.deviceSerial = q.value("device_serial").toString();
    r.deviceName = q.value("device_name").toString();
    r.userPin = q.value("user_pin").toString();
    r.punchTime = QDateTime::fromString(q.value("punch_time").toString(), "yyyy-MM-dd HH:mm:ss");
    r.verifyType = q.value("verify_type").toInt();
    r.punchStatus = q.value("punch_status").toInt();
    r.workCode = q.value("work_code").toString();
    r.rawData = q.value("raw_data").toString();
    r.createdAt = QDateTime::fromString(q.value("created_at").toString(), Qt::ISODate);
    r.errorMessage = q.value("error_message").toString();
    QString ss = q.value("sync_status").toString();
    if (ss == "synced") r.syncStatus = SyncStatus::Synced;
    else if (ss == "failed") r.syncStatus = SyncStatus::Failed;
    else r.syncStatus = SyncStatus::Pending;
    return r;
}

int Database::addAttendanceRecord(const AttendanceRecord &record) {
    QSqlQuery q(m_db);
    q.prepare(R"(
        INSERT OR IGNORE INTO attendance_records
        (device_serial, device_name, user_pin, punch_time, verify_type, punch_status,
         work_code, raw_data, sync_status)
        VALUES(?, ?, ?, ?, ?, ?, ?, ?, 'pending')
    )");
    q.addBindValue(record.deviceSerial);
    q.addBindValue(record.deviceName);
    q.addBindValue(record.userPin);
    q.addBindValue(record.punchTime.toString("yyyy-MM-dd HH:mm:ss"));
    q.addBindValue(record.verifyType);
    q.addBindValue(record.punchStatus);
    q.addBindValue(record.workCode);
    q.addBindValue(record.rawData);
    if (q.exec()) return q.lastInsertId().toInt();
    return -1;
}

bool Database::updateSyncStatus(int id, SyncStatus status, const QString &error) {
    QString statusStr = (status == SyncStatus::Synced) ? "synced"
                      : (status == SyncStatus::Failed)  ? "failed" : "pending";
    QSqlQuery q(m_db);
    q.prepare(R"(
        UPDATE attendance_records SET sync_status=?, synced_at=datetime('now'), error_message=?
        WHERE id=?
    )");
    q.addBindValue(statusStr);
    q.addBindValue(error.isEmpty() ? QVariant() : QVariant(error));
    q.addBindValue(id);
    return q.exec();
}

void Database::recordPushError(int id, const QString &error) {
    QSqlQuery q(m_db);
    q.prepare("UPDATE attendance_records SET error_message=? WHERE id=? AND sync_status='pending'");
    q.addBindValue(error);
    q.addBindValue(id);
    q.exec();
}

QList<AttendanceRecord> Database::getPendingRecords(int limit) const {
    QList<AttendanceRecord> list;
    QSqlQuery q(m_db);
    if (limit > 0) {
        q.prepare("SELECT * FROM attendance_records WHERE sync_status='pending' ORDER BY punch_time LIMIT ?");
        q.addBindValue(limit);
    } else {
        q.prepare("SELECT * FROM attendance_records WHERE sync_status='pending' ORDER BY punch_time");
    }
    if (q.exec()) while (q.next()) list.append(rowToRecord(q));
    return list;
}

QList<AttendanceRecord> Database::getRecentRecords(int limit) const {
    QList<AttendanceRecord> list;
    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM attendance_records ORDER BY punch_time DESC LIMIT ?");
    q.addBindValue(limit);
    if (q.exec()) while (q.next()) list.append(rowToRecord(q));
    return list;
}

QList<AttendanceRecord> Database::getRecordsByDevice(const QString &serial, int limit) const {
    QList<AttendanceRecord> list;
    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM attendance_records WHERE device_serial=? ORDER BY punch_time DESC LIMIT ?");
    q.addBindValue(serial);
    q.addBindValue(limit);
    if (q.exec()) while (q.next()) list.append(rowToRecord(q));
    return list;
}

int Database::getPendingCount() const {
    QSqlQuery q(m_db);
    q.exec("SELECT COUNT(*) FROM attendance_records WHERE sync_status='pending'");
    if (q.next()) return q.value(0).toInt();
    return 0;
}

int Database::getFailedCount() const {
    QSqlQuery q(m_db);
    q.exec("SELECT COUNT(*) FROM attendance_records WHERE sync_status='failed'");
    if (q.next()) return q.value(0).toInt();
    return 0;
}

bool Database::retryFailedRecords(const QList<int> &ids) {
    QSqlQuery q(m_db);
    if (ids.isEmpty()) {
        q.prepare("UPDATE attendance_records SET sync_status='pending', error_message=NULL "
                  "WHERE sync_status='failed'");
        return q.exec();
    }
    QStringList placeholders;
    for (int i = 0; i < ids.size(); ++i) placeholders << "?";
    q.prepare(QString("UPDATE attendance_records SET sync_status='pending', error_message=NULL "
                       "WHERE sync_status='failed' AND id IN (%1)").arg(placeholders.join(",")));
    for (int id : ids) q.addBindValue(id);
    return q.exec();
}

void Database::appendLog(const QString &deviceSerial, const QString &message) {
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO device_logs(device_serial, message) VALUES(?, ?)");
    q.addBindValue(deviceSerial.isEmpty() ? QVariant() : QVariant(deviceSerial));
    q.addBindValue(message);
    q.exec();
    // Keep only the last ~5000 logs. Trim occasionally rather than on every insert.
    static int sinceTrim = 0;
    if (++sinceTrim >= 200) {
        sinceTrim = 0;
        q.exec("DELETE FROM device_logs WHERE id <= (SELECT MAX(id) FROM device_logs) - 5000");
    }
}

QStringList Database::getDeviceLogs(const QString &deviceSerial, int limit) const {
    QStringList logs;
    QSqlQuery q(m_db);
    if (deviceSerial.isEmpty()) {
        q.prepare("SELECT created_at || ' | ' || COALESCE(device_serial,'SYSTEM') || ' | ' || message "
                  "FROM device_logs ORDER BY id DESC LIMIT ?");
        q.addBindValue(limit);
    } else {
        q.prepare("SELECT created_at || ' | ' || message FROM device_logs "
                  "WHERE device_serial=? ORDER BY id DESC LIMIT ?");
        q.addBindValue(deviceSerial);
        q.addBindValue(limit);
    }
    if (q.exec()) while (q.next()) logs.prepend(q.value(0).toString());
    return logs;
}

QStringList Database::getLogsAfter(int &afterId, const QString &deviceSerial) const {
    QStringList logs;
    QSqlQuery q(m_db);
    if (deviceSerial.isEmpty()) {
        q.prepare("SELECT id, created_at || ' | ' || COALESCE(device_serial,'SYSTEM') || ' | ' || message "
                  "FROM device_logs WHERE id > ? ORDER BY id LIMIT 500");
        q.addBindValue(afterId);
    } else {
        q.prepare("SELECT id, created_at || ' | ' || message FROM device_logs "
                  "WHERE id > ? AND device_serial=? ORDER BY id LIMIT 500");
        q.addBindValue(afterId);
        q.addBindValue(deviceSerial);
    }
    if (q.exec()) while (q.next()) {
        afterId = qMax(afterId, q.value(0).toInt());
        logs.append(q.value(1).toString());
    }
    return logs;
}

int Database::maxLogId() const {
    QSqlQuery q(m_db);
    if (q.exec("SELECT COALESCE(MAX(id), 0) FROM device_logs") && q.next()) return q.value(0).toInt();
    return 0;
}

int Database::maxAttendanceId() const {
    QSqlQuery q(m_db);
    if (q.exec("SELECT COALESCE(MAX(id), 0) FROM attendance_records") && q.next()) return q.value(0).toInt();
    return 0;
}

// ── Device presence ──────────────────────────────────────────────────────────

void Database::markDeviceSeen(const QString &serial, const QString &ip) {
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO device_status(serial, ip, last_seen) VALUES(?, ?, ?)");
    q.addBindValue(serial);
    q.addBindValue(ip);
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    q.exec();
}

void Database::clearDeviceSeen(const QString &serial) {
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM device_status WHERE serial = ?");
    q.addBindValue(serial);
    q.exec();
}

QHash<QString, DeviceSeen> Database::getDeviceSeen() const {
    QHash<QString, DeviceSeen> map;
    QSqlQuery q(m_db);
    if (q.exec("SELECT serial, ip, last_seen FROM device_status"))
        while (q.next()) map.insert(q.value(0).toString(), {q.value(1).toString(), q.value(2).toLongLong()});
    return map;
}

// ── Device command queue ─────────────────────────────────────────────────────

static DeviceCommand rowToCommand(QSqlQuery &q) {
    DeviceCommand c;
    c.id      = q.value("id").toInt();
    c.serial  = q.value("device_serial").toString();
    c.command = q.value("command").toString();
    c.body    = q.value("body").toString();
    c.status  = q.value("status").toString();
    c.result  = q.value("result").toString();
    return c;
}

int Database::queueDeviceCommand(const QString &serial, const QString &command, const QString &body) {
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO device_commands(device_serial, command, body) VALUES(?, ?, ?)");
    q.addBindValue(serial);
    q.addBindValue(command);
    q.addBindValue(body);
    if (q.exec()) return q.lastInsertId().toInt();
    qWarning() << "queueDeviceCommand error:" << q.lastError().text();
    return -1;
}

bool Database::takeNextCommand(const QString &serial, DeviceCommand &out) {
    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM device_commands WHERE device_serial=? AND status='pending' ORDER BY id LIMIT 1");
    q.addBindValue(serial);
    if (!q.exec() || !q.next()) return false;
    out = rowToCommand(q);
    setCommandStatus(out.id, "sent");
    out.status = "sent";
    return true;
}

DeviceCommand Database::getCommand(int id) const {
    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM device_commands WHERE id = ?");
    q.addBindValue(id);
    if (q.exec() && q.next()) return rowToCommand(q);
    return {};
}

DeviceCommand Database::latestCommand(const QString &serial, const QString &command,
                                      const QString &status) const {
    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM device_commands WHERE device_serial=? AND command=? AND status=? "
              "ORDER BY id DESC LIMIT 1");
    q.addBindValue(serial);
    q.addBindValue(command);
    q.addBindValue(status);
    if (q.exec() && q.next()) return rowToCommand(q);
    return {};
}

void Database::setCommandStatus(int id, const QString &status, const QString &result) {
    QSqlQuery q(m_db);
    q.prepare("UPDATE device_commands SET status=?, result=?, updated_at=datetime('now', 'localtime') "
              "WHERE id=?");
    q.addBindValue(status);
    q.addBindValue(result);
    q.addBindValue(id);
    q.exec();
}
