#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QList>
#include "../model/Device.h"
#include "../model/AttendanceRecord.h"

class Database : public QObject {
    Q_OBJECT
public:
    explicit Database(QObject *parent = nullptr);
    ~Database();

    bool open(const QString &path);
    void close();

    // Settings
    QString getSetting(const QString &key, const QString &defaultValue = {}) const;
    void setSetting(const QString &key, const QString &value);

    // Devices
    QList<Device> getAllDevices() const;
    Device getDevice(int id) const;
    Device getDeviceBySerial(const QString &serial) const;
    int addDevice(const Device &device);
    bool updateDevice(const Device &device);
    bool removeDevice(int id);

    // Attendance
    int addAttendanceRecord(const AttendanceRecord &record);
    bool updateSyncStatus(int id, SyncStatus status, const QString &error = {});
    QList<AttendanceRecord> getPendingRecords(int limit = 0) const;   // 0 = unlimited
    QList<AttendanceRecord> getRecentRecords(int limit = 200) const;
    QList<AttendanceRecord> getRecordsByDevice(const QString &serial, int limit = 200) const;
    int getPendingCount() const;

    // Device logs
    void appendLog(const QString &deviceSerial, const QString &message);
    QStringList getDeviceLogs(const QString &deviceSerial = {}, int limit = 500) const;

private:
    QSqlDatabase m_db;
    void createTables();
};
