import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root
    required property var backend
    signal requestRollbackConfirm()

    spacing: Kirigami.Units.largeSpacing

    Kirigami.AbstractCard {
        Layout.fillWidth: true
        leftPadding: Kirigami.Units.gridUnit
        rightPadding: Kirigami.Units.gridUnit
        topPadding: Kirigami.Units.gridUnit
        bottomPadding: Kirigami.Units.gridUnit

        contentItem: RowLayout {
            spacing: Kirigami.Units.largeSpacing

            Rectangle {
                width: 56; height: 56; radius: 28
                color: Qt.rgba(Kirigami.Theme.positiveTextColor.r,
                               Kirigami.Theme.positiveTextColor.g,
                               Kirigami.Theme.positiveTextColor.b, 0.18)
                Kirigami.Icon {
                    anchors.centerIn: parent
                    source: "qrc:/kcm/kcm_software_update/icons/check.svg"
                    isMask: true
                    width: 32; height: 32
                    color: Kirigami.Theme.positiveTextColor
                }
            }

            ColumnLayout {
                spacing: 2
                QQC2.Label {
                    text: backend.osName
                    font.bold: true
                    font.pointSize: Math.round(Kirigami.Theme.defaultFont.pointSize * 1.1)
                }
                QQC2.Label {
                    text: i18n("Your system is up to date.")
                    opacity: 0.7
                }
            }

            Item { Layout.fillWidth: true }

            QQC2.Button {
                icon.name: "view-refresh"
                text: backend.checking ? i18n("Checking…") : i18n("Check for updates")
                enabled: !backend.checking
                onClicked: backend.checkForUpdates()
            }
        }
    }

    Kirigami.AbstractCard {
        Layout.fillWidth: true
        leftPadding: Kirigami.Units.gridUnit
        rightPadding: Kirigami.Units.gridUnit
        topPadding: Kirigami.Units.gridUnit
        bottomPadding: Kirigami.Units.gridUnit

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            InfoRow {
                label: i18n("Installed")
                value: backend.osName + " " + backend.currentVersion
            }

            Kirigami.Separator { Layout.fillWidth: true }

            InfoRow {
                label: i18n("Automatic updates")
                content: RowLayout {
                    QQC2.Label {
                        text: backend.autoUpdateEnabled ? i18n("On") : i18n("Off")
                        opacity: 0.7
                    }
                    QQC2.Switch {
                        id: autoUpdateSwitch
                        onToggled: backend.setAutoUpdate(checked)
                        Binding on checked { value: backend.autoUpdateEnabled }
                    }
                }
            }

            Kirigami.Separator { Layout.fillWidth: true; visible: backend.previousVersion !== "" }

            InfoRow {
                visible: backend.previousVersion !== ""
                label: i18n("Previous version")
                extraTopMargin: Kirigami.Units.smallSpacing
                content: RowLayout {
                    QQC2.Label { text: backend.previousVersion; opacity: 0.7; Layout.topMargin: Kirigami.Units.smallSpacing }
                    QQC2.Button {
                        Layout.topMargin: Kirigami.Units.smallSpacing
                        text: i18n("Roll back")
                        onClicked: root.requestRollbackConfirm()
                    }
                }
            }
        }
    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.smallSpacing
        visible: true
        type: Kirigami.MessageType.Information
        text: i18n("Updates are applied atomically and can always be rolled back. Your files and settings are never affected.")
    }

    Item { Layout.fillHeight: true }
}
