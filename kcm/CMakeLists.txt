kcoreaddons_add_plugin(kcm_software_update
    INSTALL_NAMESPACE plasma/kcms/systemsettings
    SOURCES
        kcm_software_update.cpp
        softwareupdatebackend.cpp
)

qt_add_qml_module(kcm_software_update
    URI org.kde.kcm.softwareupdate
    VERSION 1.0
    NO_PLUGIN
    SOURCES
        softwareupdatebackend.h
    QML_FILES
        ui/main.qml
        ui/UpdateAvailable.qml
        ui/UpToDateView.qml
        ui/ProgressView.qml
        ui/DoneView.qml
        ui/RollbackConfirm.qml
        ui/RollbackDone.qml
        ui/ScheduledView.qml
        ui/ErrorView.qml
        ui/InfoRow.qml
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
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/RollbackConfirm.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/RollbackDone.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/ScheduledView.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/ErrorView.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/InfoRow.qml"
)

target_link_libraries(kcm_software_update
    PRIVATE
        Qt6::Core
        Qt6::DBus
        Qt6::Qml
        Qt6::Quick
        KF6::CoreAddons
        KF6::I18n
        KF6::KCMUtils
)

install(FILES kcm_software_update.json
    DESTINATION ${KDE_INSTALL_DATADIR}/kservices6)

ki18n_wrap_ui(kcm_software_update)
