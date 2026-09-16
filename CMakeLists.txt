cmake_minimum_required(VERSION 3.16)
project(BioSync VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Sql)

set(SOURCES
        src/assets/resources.qrc
        src/main.cpp
        src/MainWindow.cpp
        src/HeadlessRunner.cpp
        src/db/Database.cpp
        src/server/ZkAdmsServer.cpp
        src/service/ApiPusher.cpp
        src/service/Autostart.cpp
        src/service/DeviceDiscovery.cpp
        src/ui/DevicesPage.cpp
        src/ui/AttendancePage.cpp
        src/ui/LogsPage.cpp
        src/ui/SettingsPage.cpp
        src/ui/AddDeviceDialog.cpp
        src/ui/DeviceDiscoveryDialog.cpp
        src/ui/BiometricBackupDialog.cpp
)

set(HEADERS
        src/MainWindow.h
        src/HeadlessRunner.h
        src/AppPaths.h
        src/Version.h
        src/model/Device.h
        src/model/AttendanceRecord.h
        src/db/Database.h
        src/server/ZkAdmsServer.h
        src/service/ApiPusher.h
        src/service/Autostart.h
        src/service/DeviceDiscovery.h
        src/ui/DevicesPage.h
        src/ui/AttendancePage.h
        src/ui/LogsPage.h
        src/ui/SettingsPage.h
        src/ui/AddDeviceDialog.h
        src/ui/DeviceDiscoveryDialog.h
        src/ui/BiometricBackupDialog.h
        src/model/BiometricTemplate.h
)

add_executable(BioSync ${SOURCES} ${HEADERS})

target_include_directories(BioSync PRIVATE src)

target_link_libraries(BioSync PRIVATE
        Qt6::Widgets
        Qt6::Network
        Qt6::Sql
)

# App-signing secret, baked into the binary at build time (NEVER committed). RiTEMS verifies each
# push's HMAC signature against the same secret (stored in its gateway config), so only an official
# build that holds the secret can submit attendance — a leaked API key alone cannot.
#
# The secret is resolved in this order (first one found wins):
#   1. -DBIOSYNC_APP_SECRET=<secret> on the cmake command line
#   2. a `secret.env` file in the repo root  (gitignored — copy secret.env.example to it)
#   3. the BIOSYNC_APP_SECRET environment variable
if(NOT DEFINED BIOSYNC_APP_SECRET)
    if(EXISTS "${CMAKE_SOURCE_DIR}/secret.env")
        file(STRINGS "${CMAKE_SOURCE_DIR}/secret.env" _biosync_secret_lines)
        foreach(_line IN LISTS _biosync_secret_lines)
            # skip blank lines and comments, match KEY=VALUE
            if(_line MATCHES "^[ \t]*BIOSYNC_APP_SECRET[ \t]*=[ \t]*(.+)$")
                set(BIOSYNC_APP_SECRET "${CMAKE_MATCH_1}")
            endif()
        endforeach()
    elseif(DEFINED ENV{BIOSYNC_APP_SECRET})
        set(BIOSYNC_APP_SECRET "$ENV{BIOSYNC_APP_SECRET}")
    endif()
endif()

if(DEFINED BIOSYNC_APP_SECRET)
    # tolerate a trailing CR (Windows line endings) or surrounding quotes/whitespace
    string(STRIP "${BIOSYNC_APP_SECRET}" BIOSYNC_APP_SECRET)
    string(REGEX REPLACE "^\"(.*)\"$" "\\1" BIOSYNC_APP_SECRET "${BIOSYNC_APP_SECRET}")
endif()

if(DEFINED BIOSYNC_APP_SECRET AND NOT BIOSYNC_APP_SECRET STREQUAL "")
    target_compile_definitions(BioSync PRIVATE "BIOSYNC_APP_SECRET=\"${BIOSYNC_APP_SECRET}\"")
    message(STATUS "BioSync: app-signing secret configured — this is a SIGNED (official) build.")
else()
    message(WARNING "BioSync: no app-signing secret set — this build is UNSIGNED and RiTEMS will "
                    "reject its pushes. Copy secret.env.example to secret.env and set your key.")
endif()

# App version, baked in so the UI, the About panel and the update checker all report the real build.
target_compile_definitions(BioSync PRIVATE "BIOSYNC_VERSION=\"${PROJECT_VERSION}\"")

if(WIN32)
    target_sources(BioSync PRIVATE src/resources_win.rc)

    set_target_properties(BioSync PROPERTIES WIN32_EXECUTABLE TRUE)
    target_compile_definitions(BioSync PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

    get_target_property(QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QMAKE_EXECUTABLE}" DIRECTORY)

    if(NOT CMAKE_CROSSCOMPILING)
        add_custom_command(TARGET BioSync POST_BUILD
                COMMAND "${QT_BIN_DIR}/windeployqt.exe" "$<TARGET_FILE:BioSync>"
                COMMENT "Auto-deploying Qt DLLs for Windows..."
        )
    endif()
endif()

# ── Linux packaging (.deb via CPack) ──────────────────────────────────────────
if(UNIX AND NOT APPLE)
    # Install as lowercase /usr/bin/biosync (a file name — the app's display name stays "BioSync").
    set_target_properties(BioSync PROPERTIES OUTPUT_NAME biosync)
    install(TARGETS BioSync RUNTIME DESTINATION bin)
    install(FILES ${CMAKE_SOURCE_DIR}/packaging/biosync.desktop
            DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/src/assets/images/icon.png
            DESTINATION share/icons/hicolor/256x256/apps RENAME biosync.png)
    # systemd system service so BioSync runs headless at boot, before/without any login.
    install(FILES ${CMAKE_SOURCE_DIR}/packaging/biosync.service
            DESTINATION lib/systemd/system)

    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGE_NAME "biosync")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_CONTACT "Right iTech <info@ritems.io>")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Right iTech <info@ritems.io>")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "BioSync — ZKTeco biometric attendance bridge for RiTEMS")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://ritems.io")
    # Auto-resolve the exact Qt/libc runtime dependencies from the linked binary.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6sql6-sqlite")
    # postinst/postrm enable + start/stop the systemd service on install/removal.
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_SOURCE_DIR}/packaging/postinst;${CMAKE_SOURCE_DIR}/packaging/postrm")
    set(CPACK_PACKAGE_FILE_NAME "BioSync_${PROJECT_VERSION}_amd64")
    include(CPack)
endif()