#!/usr/bin/env bash
# Cross-build the BioSync Windows installer on Linux (no Windows machine needed).
# Requires: cmake, g++-mingw-w64-x86-64, nsis, and Qt 6.8.2 (host linux_gcc_64 + target
# win64_mingw) fetched with aqtinstall into $QT_ROOT.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
QT_ROOT="${QT_ROOT:-$HOME/qtcross/6.8.2}"
QT_WIN="$QT_ROOT/mingw_64"; QT_HOST="$QT_ROOT/gcc_64"

cmake -S "$HERE" -B "$HERE/build-win" \
  -DCMAKE_TOOLCHAIN_FILE="$HERE/packaging/mingw-w64.cmake" \
  -DCMAKE_PREFIX_PATH="$QT_WIN" -DQT_HOST_PATH="$QT_HOST" \
  -DCMAKE_BUILD_TYPE=Release \
  ${BIOSYNC_APP_SECRET:+-DBIOSYNC_APP_SECRET="$BIOSYNC_APP_SECRET"}
cmake --build "$HERE/build-win" -j"$(nproc)"

S="$HERE/packaging/stage"; rm -rf "$S"; mkdir -p "$S"/{platforms,sqldrivers,tls,styles,imageformats}
cp "$HERE/build-win/BioSync.exe" "$S/"
for d in Qt6Core Qt6Gui Qt6Widgets Qt6Network Qt6Sql libgcc_s_seh-1 libstdc++-6 libwinpthread-1; do cp "$QT_WIN/bin/$d.dll" "$S/"; done
cp "$QT_WIN/plugins/platforms/qwindows.dll"        "$S/platforms/"
cp "$QT_WIN/plugins/sqldrivers/qsqlite.dll"        "$S/sqldrivers/"
cp "$QT_WIN/plugins/tls/qschannelbackend.dll"      "$S/tls/"
cp "$QT_WIN/plugins/styles/qmodernwindowsstyle.dll" "$S/styles/" || true
cp "$QT_WIN/plugins/imageformats/qico.dll"         "$S/imageformats/" || true
cp "$HERE/src/assets/images/icon.ico" "$HERE/packaging/icon.ico"
( cd "$HERE/packaging" && makensis -V2 biosync.nsi )
echo "Installer: $HERE/packaging/BioSync_Setup_v1.1.0.exe"
