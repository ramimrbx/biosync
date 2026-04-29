#include "DeviceDiscovery.h"
#include <QNetworkInterface>
#include <QAbstractSocket>
#include <QRegularExpression>
#include <QDebug>

/*
 * ZKTeco UDP Discovery Protocol
 *
 * ZKTeco devices listen on UDP port 4370. When you broadcast a "search"
 * command packet to the subnet, any device on the same network responds
 * with a packet containing its serial number, model, IP, and MAC.
 *
 * The command packet is 8 bytes (ZKTeco binary protocol header):
 *   Byte 0-1: 0x00 0x00  (start marker / reserved)
 *   Byte 2-3: 0x00 0x00  (session ID = 0 for discovery)
 *   Byte 4-5: 0x00 0x00  (reply counter = 0)
 *   Byte 6-7: 0xE8 0x03  (command = 1000 = CMD_CONNECT, little-endian)
 *
 * The device responds with a packet whose payload is a null-separated
 * string containing key=value pairs, e.g.:
 *   "SN=ABC1234567\x00~Model=K40\x00~..."
 *
 * Some older devices respond with a different layout — we handle both
 * with a heuristic parser that looks for "SN=" anywhere in the payload.
 */

static const QByteArray DISCOVERY_PACKET = QByteArray::fromHex("0000000000000003E8");

// Older ZKTeco devices also respond to this 16-byte broadcast:
static const QByteArray DISCOVERY_PACKET_ALT = QByteArray(16, '\x00');

DeviceDiscovery::DeviceDiscovery(QObject *parent) : QObject(parent) {}

void DeviceDiscovery::startScan() {
    if (m_scanning) return;
    m_scanning = true;
    m_found = 0;

    m_udpSocket = new QUdpSocket(this);
    m_udpSocket->bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress);
    connect(m_udpSocket, &QUdpSocket::readyRead, this, &DeviceDiscovery::onUdpResponse);

    m_scanTimer = new QTimer(this);
    m_scanTimer->setSingleShot(true);
    m_scanTimer->setInterval(5000); // 5-second scan window
    connect(m_scanTimer, &QTimer::timeout, this, &DeviceDiscovery::onScanTimeout);

    sendUdpBroadcast();
    m_scanTimer->start();

    emit progress(10, "Broadcasting discovery packet...");
}

void DeviceDiscovery::stopScan() {
    onScanTimeout();
}

void DeviceDiscovery::sendUdpBroadcast() {
    // Broadcast the discovery packet to the whole subnet on port 4370
    m_udpSocket->writeDatagram(DISCOVERY_PACKET, QHostAddress::Broadcast, 4370);
    m_udpSocket->writeDatagram(DISCOVERY_PACKET_ALT, QHostAddress::Broadcast, 4370);

    // Also send to each specific subnet broadcast address for multi-interface machines
    for (const QHostAddress &bcast : localSubnetAddresses()) {
        m_udpSocket->writeDatagram(DISCOVERY_PACKET, bcast, 4370);
        m_udpSocket->writeDatagram(DISCOVERY_PACKET_ALT, bcast, 4370);
    }
}

void DeviceDiscovery::onUdpResponse() {
    while (m_udpSocket->hasPendingDatagrams()) {
        QByteArray data;
        data.resize(m_udpSocket->pendingDatagramSize());
        QHostAddress sender;
        quint16 senderPort;
        m_udpSocket->readDatagram(data.data(), data.size(), &sender, &senderPort);

        // Ignore our own broadcast echo
        if (data == DISCOVERY_PACKET || data == DISCOVERY_PACKET_ALT) continue;
        if (data.size() < 4) continue;

        DiscoveredDevice dev = parseUdpResponse(data, sender);
        if (!dev.serialNumber.isEmpty()) {
            m_found++;
            emit deviceFound(dev);
            emit progress(50, QString("Found device: %1 (%2)").arg(dev.serialNumber, sender.toString()));
        }
    }
}

void DeviceDiscovery::onScanTimeout() {
    m_scanning = false;
    if (m_udpSocket) { m_udpSocket->close(); m_udpSocket->deleteLater(); m_udpSocket = nullptr; }
    if (m_scanTimer) { m_scanTimer->stop(); m_scanTimer->deleteLater(); m_scanTimer = nullptr; }
    emit progress(100, QString("Scan complete. %1 device(s) found.").arg(m_found));
    emit scanFinished(m_found);
}

DiscoveredDevice DeviceDiscovery::parseUdpResponse(const QByteArray &data, const QHostAddress &sender) {
    DiscoveredDevice dev;
    dev.ipAddress = sender.toString();
    dev.port = 4370;

    /*
     * ZKTeco response payload examples:
     *
     *  Format A (newer devices):
     *    \x00\x00\x00\x00\x00\x00SN=ABC1234567\x00~Model=K40\x00~...
     *
     *  Format B (ADMS response embedded in URL-encoded string):
     *    SN=ABC1234567&Stamp=...&DeviceName=...
     *
     *  Format C (raw null-separated):
     *    ABC1234567\x00K40\x00192.168.1.201\x00...
     *
     * We use a heuristic: look for "SN=" anywhere in the payload.
     * If not found, the first null-terminated string may be the serial.
     */

    QString text = QString::fromLatin1(data);

    // Try "SN=VALUE" pattern first (most reliable)
    QRegularExpression snRe("SN=([A-Za-z0-9_\\-]{4,32})");
    auto snMatch = snRe.match(text);
    if (snMatch.hasMatch()) {
        dev.serialNumber = snMatch.captured(1);
    }

    // Try "DeviceName=" or "Model="
    QRegularExpression modelRe("(?:DeviceName|Model)=([^&\\x00~]{1,32})");
    auto modelMatch = modelRe.match(text);
    if (modelMatch.hasMatch()) {
        dev.model = modelMatch.captured(1).trimmed();
    }

    // Try "IP=" override (device may report its own IP)
    QRegularExpression ipRe("IP=([\\d\\.]{7,15})");
    auto ipMatch = ipRe.match(text);
    if (ipMatch.hasMatch()) {
        dev.ipAddress = ipMatch.captured(1);
    }

    // Format C fallback: if no SN= found, try first printable null-terminated token
    if (dev.serialNumber.isEmpty()) {
        // Skip any binary header bytes (first 8 bytes are usually the response header)
        int offset = (data.size() > 8) ? 8 : 0;
        QByteArray payload = data.mid(offset);
        QString first = QString::fromLatin1(payload.split('\x00').first()).trimmed();
        // A serial number is typically alphanumeric, 4–20 chars, no spaces
        if (!first.isEmpty() && first.length() <= 20 &&
            first.contains(QRegularExpression("^[A-Za-z0-9_\\-]+$"))) {
            dev.serialNumber = first;
        }
    }

    return dev;
}

QList<QHostAddress> DeviceDiscovery::localSubnetAddresses() const {
    QList<QHostAddress> bcasts;
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        if (!iface.flags().testFlag(QNetworkInterface::IsUp) ||
            iface.flags().testFlag(QNetworkInterface::IsLoopBack)) continue;
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol &&
                !entry.broadcast().isNull()) {
                bcasts.append(entry.broadcast());
            }
        }
    }
    return bcasts;
}
