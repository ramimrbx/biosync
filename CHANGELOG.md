# BioSync — Changelog

All notable changes to BioSync are recorded here. Installing a newer version over an older one
upgrades in place — your configuration, devices and pending records are kept.

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
