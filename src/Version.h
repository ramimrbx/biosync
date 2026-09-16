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
// App metadata — injected from appinfo.cmake at build time (see CMakeLists.txt).
#ifndef APP_NAME
#define APP_NAME "BioSync"
#endif
#ifndef APP_PUBLISHER
#define APP_PUBLISHER "Right iTech"
#endif
#ifndef APP_URL
#define APP_URL "https://ritems.io"
#endif
#ifndef SUPPORT_EMAIL
#define SUPPORT_EMAIL ""
#endif
#ifndef SUPPORT_PHONE
#define SUPPORT_PHONE ""
#endif

namespace BioSync {

inline QString version()       { return QStringLiteral(BIOSYNC_VERSION); }
inline QString appName()       { return QStringLiteral(APP_NAME); }
inline QString publisher()     { return QStringLiteral(APP_PUBLISHER); }
inline QString appUrl()        { return QStringLiteral(APP_URL); }
inline QString supportEmail()  { return QStringLiteral(SUPPORT_EMAIL); }
inline QString supportPhone()  { return QStringLiteral(SUPPORT_PHONE); }

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
