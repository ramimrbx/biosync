#pragma once
#include <QString>
#include <QDateTime>

struct Device {
    int id = 0;
    QString name;
    QString serialNumber;
    QString ipAddress;   // configured IP stored in database
    int port = 4370;
    QString location;
    QString notes;
    bool isActive = true;
    QDateTime createdAt;

    // Runtime-only fields (not persisted)
    bool isOnline = false;
    QDateTime lastSeen;
    QString socketIp;    // actual IP from which the device connected via ADMS
};
