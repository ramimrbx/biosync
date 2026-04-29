#pragma once
#include <QString>
#include <QDateTime>

enum class SyncStatus { Pending, Synced, Failed };

struct AttendanceRecord {
    int id = 0;
    QString deviceSerial;
    QString deviceName;
    QString userPin;
    QDateTime punchTime;
    int verifyType = 0;
    int punchStatus = 0;
    QString workCode;
    QString rawData;
    SyncStatus syncStatus = SyncStatus::Pending;
    QDateTime createdAt;
    QDateTime syncedAt;
    QString errorMessage;

    QString verifyTypeName() const {
        switch (verifyType) {
            case 1: return "Finger";
            case 4: return "Face";
            case 15: return "Password";
            default: return "Unknown";
        }
    }

    QString punchStatusName() const {
        switch (punchStatus) {
            case 0: return "Check In";
            case 1: return "Check Out";
            case 2: return "Break Out";
            case 3: return "Break In";
            case 4: return "OT In";
            case 5: return "OT Out";
            default: return "Unknown";
        }
    }

    QString syncStatusName() const {
        switch (syncStatus) {
            case SyncStatus::Synced: return "Synced";
            case SyncStatus::Failed: return "Failed";
            default: return "Pending";
        }
    }
};
