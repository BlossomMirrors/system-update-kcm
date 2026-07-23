kcoreaddons_add_plugin(kcm_software_update
    INSTALL_NAMESPACE plasma/kcms/systemsettings
    SOURCES
        kcm_software_update.cpp
        softwareupdatebackend.cpp
)

qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

qt_add_qml_module(kcm_software_update
    URI org.kde.kcm.softwareupdate
    VERSION 1.0
    NO_PLUGIN
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/org/kde/kcm/softwareupdate"
    SOURCES
        softwareupdatebackend.h
    QML_FILES
        ui/main.qml
        ui/UpdateAvailable.qml
        ui/UpToDateView.qml
        ui/ProgressView.qml
        ui/DoneView.qml
        ui/RollbackDone.qml
        ui/ScheduledView.qml
        ui/ErrorView.qml
        ui/InfoRow.qml
)

qt_add_resources(kcm_software_update "kcm_icons"
    PREFIX "/kcm/kcm_software_update"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES
        icons/monitor.svg
        icons/check.svg
        icons/download.svg
        icons/clock.svg
        icons/rotate-ccw.svg
        icons/alert-circle.svg
        icons/refresh-cw.svg
        icons/chevron-down.svg
        icons/chevron-right.svg
        icons/x.svg
)

qt_add_resources(kcm_software_update "qml_kcm_path"
    PREFIX "/kcm/kcm_software_update"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/ui"
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/main.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/UpdateAvailable.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/UpToDateView.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/ProgressView.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/DoneView.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/RollbackDone.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/ScheduledView.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/ErrorView.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/InfoRow.qml"
)

target_compile_definitions(kcm_software_update
    PRIVATE TRANSLATION_DOMAIN="kcm_software_update")

target_link_libraries(kcm_software_update
    PRIVATE
        Qt6::Core
        Qt6::DBus
        Qt6::Network
        Qt6::Qml
        Qt6::Quick
        KF6::CoreAddons
        KF6::I18n
        KF6::KCMUtils
)

install(FILES kcm_software_update.json
    DESTINATION ${KDE_INSTALL_DATADIR}/kservices6)

ki18n_wrap_ui(kcm_software_update)
