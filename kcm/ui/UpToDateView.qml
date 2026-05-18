import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root
    required property var backend
    signal requestRollbackConfirm()

    spacing: Kirigami.Units.largeSpacing

    Rectangle {
        Layout.fillWidth: true
        color: Kirigami.Theme.backgroundColor
        border.color: Qt.rgba(Kirigami.Theme.textColor.r,
                              Kirigami.Theme.textColor.g,
                              Kirigami.Theme.textColor.b, 0.12)
        border.width: 1
        radius: Kirigami.Units.largeSpacing
        implicitHeight: headerRow.implicitHeight + Kirigami.Units.gridUnit * 2

        RowLayout {
            id: headerRow
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: Kirigami.Units.gridUnit }
            spacing: Kirigami.Units.largeSpacing

            Rectangle {
                width: 48; height: 48; radius: 24
                color: Qt.rgba(Kirigami.Theme.positiveTextColor.r,
                               Kirigami.Theme.positiveTextColor.g,
                               Kirigami.Theme.positiveTextColor.b, 0.18)
                Kirigami.Icon {
                    anchors.centerIn: parent
                    source: "security-high"
                    width: 24; height: 24
                    color: Kirigami.Theme.positiveTextColor
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
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

            QQC2.Button {
                text: i18n("Check for updates")
                icon.name: "view-refresh"
                onClicked: backend.checkForUpdates()
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        color: Kirigami.Theme.backgroundColor
        border.color: Qt.rgba(Kirigami.Theme.textColor.r,
                              Kirigami.Theme.textColor.g,
                              Kirigami.Theme.textColor.b, 0.12)
        border.width: 1
        radius: Kirigami.Units.largeSpacing
        implicitHeight: infoCol.implicitHeight

        ColumnLayout {
            id: infoCol
            anchors { left: parent.left; right: parent.right }
            spacing: 0

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
                        checked: backend.autoUpdateEnabled
                        onToggled: backend.setAutoUpdate(checked)
                    }
                }
            }

            Kirigami.Separator { Layout.fillWidth: true; visible: backend.previousVersion !== "" }

            InfoRow {
                visible: backend.previousVersion !== ""
                label: i18n("Previous version")
                content: RowLayout {
                    QQC2.Label { text: backend.previousVersion; opacity: 0.7 }
                    QQC2.Button {
                        text: i18n("Roll back")
                        onClicked: root.requestRollbackConfirm()
                    }
                }
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        color: Kirigami.Theme.backgroundColor
        border.color: Qt.rgba(Kirigami.Theme.textColor.r,
                              Kirigami.Theme.textColor.g,
                              Kirigami.Theme.textColor.b, 0.12)
        border.width: 1
        radius: Kirigami.Units.largeSpacing
        implicitHeight: discLabel.implicitHeight + Kirigami.Units.gridUnit * 2

        QQC2.Label {
            id: discLabel
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: Kirigami.Units.gridUnit }
            text: i18n("Updates are applied atomically and can always be rolled back. Your files and settings are never affected.")
            wrapMode: Text.WordWrap
            opacity: 0.7
        }
    }

    Item { Layout.fillHeight: true }
}
