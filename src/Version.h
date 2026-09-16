#pragma once
#include <QByteArray>
#include <QCryptographicHash>
#include <QString>

// These are normally injected at build time by CMake (target_compile_definitions). The fallbacks
// keep the app compilable when configured without them (an unofficial / unsigned dev build).
#ifndef BIOSYNC_VERSION
#define BIOSYNC_VERSION "dev"
#endif
#ifndef BIOSYNC_APP_SECRET
#define BIOSYNC_APP_SECRET ""
#endif

namespace BioSync {

inline QString version() { return QStringLiteral(BIOSYNC_VERSION); }

// True when this build was compiled with the shared app secret (i.e. an official/signed build whose
// pushes RiTEMS will accept).
inline bool isSigned() { return !QByteArrayLiteral(BIOSYNC_APP_SECRET).isEmpty(); }

// A short, non-reversible fingerprint of the secret. Safe to display: it lets an operator confirm
// two builds share the same secret without ever exposing the secret itself. Empty when unsigned.
inline QString secretFingerprint() {
    const QByteArray s = QByteArrayLiteral(BIOSYNC_APP_SECRET);
    if (s.isEmpty()) return QString();
    return QString::fromLatin1(
        QCryptographicHash::hash(s, QCryptographicHash::Sha256).toHex().left(8));
}

} // namespace BioSync
