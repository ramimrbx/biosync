#pragma once
#include <QString>

struct BiometricTemplate {
    QString userPin;
    int     fingerIndex  = 0;   // 0-9: fingers, 10: face
    QString templateData;       // Base64-encoded raw template bytes
    int     templateSize = 0;   // byte size reported by device
    bool    valid        = true;
};
