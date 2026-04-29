#include "Database.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>
#include <QDir>
#include <QFileInfo>

Database::Database(QObject *parent) : QObject(parent) {}

Database::~Database() {
    close();
}

bool Database::open(const QString &path) {
    QDir().mkpath(QFileInfo(path).absolutePath());

    m_db = QSqlDatabase::addDatabase("QSQLITE", "biosync");
    m_db.setDatabaseName(path);

    if (!m_db.open()) {
        qCritical() << "Failed to open database:" << m_db.lastError().text();
        return false;
    }

    QSqlQuery q(m_db);
    q.exec("PRAGMA journal_mode=WAL");
    q.exec("PRAGMA foreign_keys=ON");

    createTables();
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

    if (q.lastError().isValid())
        qWarning() << "Table creation error:" << q.lastError().text();
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

void Database::appendLog(const QString &deviceSerial, const QString &message) {
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO device_logs(device_serial, message) VALUES(?, ?)");
    q.addBindValue(deviceSerial.isEmpty() ? QVariant() : QVariant(deviceSerial));
    q.addBindValue(message);
    q.exec();
    // Keep only last 5000 logs
    q.exec("DELETE FROM device_logs WHERE id NOT IN (SELECT id FROM device_logs ORDER BY id DESC LIMIT 5000)");
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
