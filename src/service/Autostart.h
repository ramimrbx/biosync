#pragma once
#include <QString>

/**
 * Whether BioSync launches automatically when the machine starts.
 *
 * BioSync is the ADMS server the attendance devices push to, so it needs to be running whenever the
 * institution's computer is on — not only when someone remembers to open it. This toggles the
 * platform's per-user autostart, no administrator rights required.
 *
 *   Windows: an HKCU\...\CurrentVersion\Run value pointing at this executable.
 *   Linux:   a ~/.config/autostart/biosync.desktop entry.
 */
class Autostart {
public:
    /** Enable or disable launch-at-startup for the current user. Returns true on success. */
    static bool setEnabled(bool enable);

    /** Whether launch-at-startup is currently enabled for the current user. */
    static bool isEnabled();

private:
    static QString appPath();          // this executable, native separators
    static const char *ENTRY_NAME;     // "BioSync"
};
