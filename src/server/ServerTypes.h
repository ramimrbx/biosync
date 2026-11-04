#pragma once
#include <QString>
#include <QHash>
#include <QByteArray>
#include <functional>

struct ParsedRequest {
    QString method;
    QString path;
    QHash<QString, QString> queryParams;
    QHash<QString, QString> headers;   // keys are lower case
    QByteArray body;
    bool valid = false;
};

// A command to be delivered to a device: on its next getrequest poll (ZKTeco ADMS), or pushed
// straight down its WebSocket (RitOS). body contains optional data lines appended after the
// command header (e.g. for DATA UPDATE). id is sent to the device as the command ID and echoed
// back in its result.
struct QueuedCommand {
    int     id = 0;
    QString command;
    QString body;
};

// Supplies the next command for a device (returns false when there is none).
using CommandProvider = std::function<bool(const QString &serial, QueuedCommand &out)>;
