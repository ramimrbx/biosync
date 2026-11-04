#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QList>
#include <QHash>
#include "../model/Device.h"
#include "../model/AttendanceRecord.h"

// A command for a device, queued in the shared DB so whichever BioSync process currently owns the
// attendance server (the background service or the window) delivers it on the device's next poll.
// status: pending → sent → (uploading →) done | failed
struct DeviceCommand {
    int     id = 0;
    QString serial;
    QString command;
    QString body;
    QString status;
    QString result;
};

// Last time a device talked to whichever BioSync process owns the attendance server.
struct DeviceSeen {
    QString ip;
    qint64  lastSeenMs = 0;
};

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
    int getFailedCount() const;
    // Resets 'failed' rows back to 'pending' so the next push retries them. Empty ids retries
    // every failed row; otherwise only the given ids (still gated to currently-failed rows).
    bool retryFailedRecords(const QList<int> &ids = {});

    int maxAttendanceId() const;

    // Device logs
    void appendLog(const QString &deviceSerial, const QString &message);
    QStringList getDeviceLogs(const QString &deviceSerial = {}, int limit = 500) const;
    // Log lines written after `afterId` (same format as getDeviceLogs); updates afterId.
    QStringList getLogsAfter(int &afterId, const QString &deviceSerial = {}) const;
    int maxLogId() const;

    // Device presence (written by the server owner, read by every window)
    void markDeviceSeen(const QString &serial, const QString &ip);
    void clearDeviceSeen(const QString &serial);
    QHash<QString, DeviceSeen> getDeviceSeen() const;

    // Device command queue
    int  queueDeviceCommand(const QString &serial, const QString &command, const QString &body = {});
    bool takeNextCommand(const QString &serial, DeviceCommand &out);   // pending → sent
    DeviceCommand getCommand(int id) const;
    DeviceCommand latestCommand(const QString &serial, const QString &command,
                                const QString &status) const;
    void setCommandStatus(int id, const QString &status, const QString &result = {});

private:
    QSqlDatabase m_db;
    void createTables();
};
