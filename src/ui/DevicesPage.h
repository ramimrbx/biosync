#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QHash>
#include <QSet>
#include "../db/Database.h"

class DevicesPage : public QWidget {
    Q_OBJECT
public:
    explicit DevicesPage(Database *db, QWidget *parent = nullptr);

    void refresh();
    void markDeviceOnline(const QString &serial, bool online);
    void setDeviceSocketIp(const QString &serial, const QString &ip);

signals:
    void devicesChanged();
    void scanNetworkRequested();
    void activateDeviceRequested(int deviceId);
    void deactivateDeviceRequested(int deviceId);
    void reconnectDeviceRequested(const QString &serial);
    void restartDeviceRequested(const QString &serial);
    void backupTemplatesRequested(const QString &serial);
    void restoreTemplatesRequested(const QString &serial);

private slots:
    void onAddDevice();
    void onEditDevice();
    void onRemoveDevice();
    void onContextMenuRequested(const QPoint &pos);

private:
    Database      *m_db;
    QTableWidget  *m_table;
    QPushButton   *m_btnEdit;
    QPushButton   *m_btnRemove;
    QWidget       *m_cardTotal;
    QWidget       *m_cardOnline;
    QWidget       *m_cardOff;
    QHash<QString, int>     m_serialToRow;
    QSet<QString>           m_onlineSet;
    QHash<QString, QString> m_socketIpMap;

    void populateTable(const QList<Device> &devices);
    void updateStatCards();
    Device selectedDevice() const;
    QWidget *makeStatCard(const QString &label, const QString &value, const QString &color);
};
