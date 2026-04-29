cmake_minimum_required(VERSION 3.16)
project(BioSync VERSION 1.0.0 LANGUAGES CXX)

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
        src/db/Database.cpp
        src/server/ZkAdmsServer.cpp
        src/service/ApiPusher.cpp
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
        src/model/Device.h
        src/model/AttendanceRecord.h
        src/db/Database.h
        src/server/ZkAdmsServer.h
        src/service/ApiPusher.h
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

if(WIN32)
    set_target_properties(BioSync PROPERTIES WIN32_EXECUTABLE TRUE)
    target_compile_definitions(BioSync PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()