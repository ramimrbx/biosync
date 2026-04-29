#pragma once
#include <QObject>
#include <QUdpSocket>
#include <QTcpSocket>
#include <QTimer>
#include <QHostAddress>
#include <QList>

struct DiscoveredDevice {
    QString ipAddress;
    QString serialNumber;
    QString model;
    int port = 4370;
};

class DeviceDiscovery : public QObject {
    Q_OBJECT
public:
    explicit DeviceDiscovery(QObject *parent = nullptr);

    void startScan();
    void stopScan();
    bool isScanning() const { return m_scanning; }

signals:
    void deviceFound(DiscoveredDevice device);
    void scanFinished(int totalFound);
    void progress(int percent, const QString &status);

private slots:
    void onUdpResponse();
    void onScanTimeout();

private:
    QUdpSocket *m_udpSocket = nullptr;
    QTimer     *m_scanTimer = nullptr;
    bool        m_scanning  = false;
    int         m_found     = 0;

    QList<QHostAddress> localSubnetAddresses() const;
    void sendUdpBroadcast();
    DiscoveredDevice parseUdpResponse(const QByteArray &data, const QHostAddress &sender);
};
