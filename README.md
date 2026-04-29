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

## API Configuration
1. Set `biosync.api-key` in `api/src/main/resources/application.properties`
2. In BioSync Settings:
   - API Base URL: `http://<api-server>:8080`
   - API Key: same value as `biosync.api-key`
   - Institution ID: the institution's ID in the database

## Offline Mode
BioSync stores all attendance records locally in SQLite (`~/.biosync/biosync.db` on Linux,
`%APPDATA%\RightITech\BioSync\biosync.db` on Windows). When the API is unreachable,
records are queued with `sync_status = pending` and automatically pushed when connectivity
is restored (checked every 15 seconds).
