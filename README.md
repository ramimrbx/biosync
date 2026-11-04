# BioSync — Build Instructions

## Dependencies
- Qt 6.4+ (Widgets, Network, Sql modules)
- CMake 3.16+
- C++17 compiler

## Linux (Ubuntu/Debian)
```bash
sudo apt install cmake g++ qt6-base-dev qt6-base-private-dev
cd biosync
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/BioSync
```

## Windows (MSYS2/MinGW)
```bash
pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-qt6-base
cmake -B build -DCMAKE_BUILD_TYPE=Release -G "MinGW Makefiles"
cmake --build build -j4
build\BioSync.exe
```

## Windows (MSVC + Qt installer)
1. Install Qt 6 from https://www.qt.io/download
2. Install CMake
3. Open CMakeLists.txt in Qt Creator or run:
```bat
cmake -B build -DCMAKE_PREFIX_PATH=C:\Qt\6.x.x\msvc2022_64
cmake --build build --config Release
```

## ZKTeco Device Setup
1. Run BioSync and note your machine's IP in Settings
2. Set the ADMS server port in BioSync Settings (default: 8085)
3. On each ZKTeco device, go to **Communication > ADMS** (or Cloud):
   - Server Address: `<this machine's IP>`
   - Port: `8085` (or your configured port)
   - Enable HTTPS: Off
4. The device will appear Online in BioSync when it sends the first heartbeat

## RitOS (BioSync hardware) Device Setup
RitOS devices connect to the **same port** as ZKTeco devices, but over a WebSocket instead of
ADMS HTTP polling. BioSync tells them apart automatically, so both kinds can be used together.
1. On the device, open **Settings > Server**
2. **Server IP or name:** `<this machine's IP>`, **Port:** `8085` (or your configured port).
   You can also type `192.168.0.10:8085` in the IP field. Any **WebSocket path** works.
3. **Data format:** JSON or Text, BioSync understands both
4. Turn on **Send to server**. The device is registered automatically on its first connection,
   with its device ID (for example `BioSync-C8A85C`) as the serial number.

Everything available for ZKTeco devices works for RitOS too: live online status, punches (stored
locally, confirmed back to the device, then pushed to RiTEMS), **Restart device**,
**Backup templates**, **Restore templates**, and **Scan network** (the device answers the UDP
discovery broadcast on port 4370). The wire protocol is described in `src/server/RitosLink.h`
and in the RitOS README.

**Interactive attendance on the device:** turn on *Settings > Attendance > Interactive attendance*
on the RitOS device. After a punch the device asks BioSync, which calls
`POST /api/v1/biosync/punch-context` on RiTEMS, what to ask. The person answers on the device, and
the punch is pushed with an `answer`, so RiTEMS classifies it directly with no browser or Android
popup. If RiTEMS is unreachable, too old or doesn't answer, the punch goes through as a normal one
and RiTEMS shows its popup as before.

**User names:** the attendance table shows the person's name. RitOS sends it with each punch.
ZKTeco devices are asked for their user list (`DATA QUERY USERINFO`) once per session, and new
enrolments arrive in their OPERLOG.

## API Configuration
Set the shared API key on the RiTEMS side (the database value takes priority if both are set):
- **Recommended:** in RiTEMS go to **Settings → Gateway** and add a system config with
  key `biosync-api-key` and your chosen secret value (stored encrypted).
- Or set `biosync.api-key` in the API's `application.properties`.

Then in BioSync **Settings**:
- **API Base URL:** your RiTEMS URL, e.g. `https://ritems.io` (BioSync appends the
  `/api/v1/biosync/...` paths itself — do not include a path here)
- **API Key:** the same value as `biosync-api-key`
- **Institution ID:** the institution's ID in RiTEMS

BioSync talks to RiTEMS over these endpoints, all authenticated with the `X-BioSync-Key`
header (no login needed):
- `POST /api/v1/biosync/push` — attendance punches
- `POST /api/v1/biosync/templates/backup` — back up fingerprint/face templates
- `GET  /api/v1/biosync/templates/device/{serial}` — fetch templates to restore to a device

## Offline Mode
BioSync stores all attendance records locally in SQLite (`~/.biosync/biosync.db` on Linux,
`%APPDATA%\Right iTech\BioSync\biosync.db` on Windows). When the API is unreachable,
records are queued with `sync_status = pending` and automatically pushed when connectivity
is restored (checked every 15 seconds).
