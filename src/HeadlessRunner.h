#pragma once
#include <QObject>
#include "db/Database.h"
#include "engine/Engine.h"

class QTimer;

// The background service (--headless): runs the attendance engine with NO GUI, at boot, before any
// login. It is the preferred owner of the server port. If the port is busy at startup (a BioSync
// window grabbed it while the service was down), it does NOT exit: it asks the window to hand the
// port over (setting "service_waiting") and keeps retrying until it gets it.
class HeadlessRunner : public QObject {
    Q_OBJECT
public:
    explicit HeadlessRunner(QObject *parent = nullptr);
    // Returns false only on a fatal error (the database can't be opened).
    bool start();

private:
    void tryStart();
    void log(const QString &msg);

    Database *m_db;
    Engine   *m_engine = nullptr;
    QTimer   *m_retry  = nullptr;
    bool      m_waiting = false;
    bool      m_conflictLogged = false;
};
