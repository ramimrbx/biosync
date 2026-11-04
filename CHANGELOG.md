# BioSync — Changelog

All notable changes to BioSync are recorded here. Installing a newer version over an older one
upgrades in place — your configuration, devices and pending records are kept.

## 1.4.1 — 2026-11-04
Everything from 1.3.2 (new design, splash screen, light/dark theme, icons) plus:
- **No more "Attendance server failed to start on port 8085 — address already in use".** The
  background service and the BioSync window no longer fight over the port. The service owns it; when
  you open the window it detects the service and shows its live status (devices, punches, logs)
  instead of trying to start a second server. If the service isn't running, the window runs the
  server itself — and hands it back automatically when the service starts.
- **Only one BioSync window.** Opening BioSync again brings the open window to the front instead of
  starting another copy (a silent sign-in launch just stays in the tray).
- **Punches are never lost when a sync fails.** Before, a single failed send (a short internet drop,
  server restart, timeout) marked punches failed and they were never sent again. Now they stay
  queued and retry automatically (15s, 30s, 1m … up to every 5 min) until RiTEMS accepts them;
  punches stuck by earlier versions are re-queued once on upgrade. Large backlogs go in batches of 500.
- **Background service never gives up.** If the port is busy at boot it waits and retries instead of
  exiting; on Windows the startup task is no longer stopped after 3 days of uptime or on battery.
- **Device commands work while the service runs.** Restart device, biometric backup and restore are
  delivered through the background service.
- Restart and port changes in Settings apply to the background service within seconds.

## 1.3.2 — 2026-09-17
- **Fixed "address already in use" after a manual update** — installing a new version by
  downloading and running the installer directly (rather than through the in-app updater) could
  leave the previous version's process — the interactive app (it keeps running in the tray after
  the window is closed) and/or the headless startup service — still running and holding the ADMS
  port, so the new version failed to bind it. The installer now stops both before installing
  files, so a direct download-and-run upgrades exactly as cleanly as the in-app updater does.
- **Server start/stop/restart now always shows what happened** — pressing Start, Stop or Restart
  on the Settings page (and a failed auto-start at launch) now shows a clear confirmation or error,
  including the actual reason for a failure, as a toast and a tray notification, instead of only
  being visible on the Logs page.

## 1.3.1 — 2026-09-17
- **Fixed harsh selection color** — clicking a device or attendance row no longer flashes solid
  purple; rows now highlight with the same soft accent tint used elsewhere in the UI, and rows
  also highlight softly on hover.

## 1.3.0 — 2026-09-17
- **Redesigned UI** — every screen now follows ritems' web client visual language: the same accent
  color, surfaces, borders and typography, restyled buttons/inputs/tables/dialogs, and a sidebar
  that mirrors ritems' nav (bordered rail, rounded active/hover pills, left accent bar, two-tone
  wordmark).

## 1.2.0 — 2026-09-16
- **About panel** — click the version at the bottom of the sidebar to see the build version, whether
  it is an official **signed** build (with a safe key fingerprint), and the configured server.
- **Check for updates + auto-update** — BioSync now checks RiTEMS for a newer published version on
  startup and once a day, and you can check on demand from the About panel. When a newer version
  exists it shows the changelog and can download the installer and launch it to update in place.
- **Restart feedback** — the server Restart button now confirms in the window (and via a tray
  notification) that the attendance server restarted and on which port, instead of appearing to do
  nothing.
- The sidebar now shows the real build version (it was hard-coded before).

## 1.1.0 — 2026-09-16
- **Headless boot service** — BioSync now runs without any login and without opening a window. On
  Linux it installs a systemd service; on Windows it registers a startup task that runs as SYSTEM.
  The attendance bridge is up as soon as the PC powers on.
- **Signed pushes** — every attendance push is now cryptographically signed (HMAC-SHA256). RiTEMS
  rejects any push that isn't signed by a genuine BioSync build, so a leaked API key alone can no
  longer be used to submit fake attendance.
- **Shared machine-wide config** — the GUI (used to configure) and the boot service read the same
  settings, migrated automatically from the previous per-user location.
- Upgrades install in place, keeping configuration, devices and any pending records.

## 1.0.0 — 2026-09-09
- First release: ZKTeco ADMS bridge — receives punches from biometric devices and pushes them to
  RiTEMS. Device discovery, per-device management, live attendance view, biometric-template backup,
  and Windows-firewall auto-authorisation.
