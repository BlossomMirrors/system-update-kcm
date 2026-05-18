import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root
    required property var backend
    signal requestRollbackConfirm()
    signal requestScheduled()

    spacing: Kirigami.Units.largeSpacing

    // Top card: OS name + version + action buttons
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
            anchors {
                left: parent.left; right: parent.right; top: parent.top
                margins: Kirigami.Units.gridUnit
            }
            spacing: Kirigami.Units.largeSpacing

            Rectangle {
                width: 48; height: 48; radius: 24
                color: Qt.rgba(Kirigami.Theme.highlightColor.r,
                               Kirigami.Theme.highlightColor.g,
                               Kirigami.Theme.highlightColor.b, 0.18)
                Kirigami.Icon {
                    anchors.centerIn: parent
                    source: "computer-symbolic"
                    width: 24; height: 24
                    color: Kirigami.Theme.highlightColor
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
                    text: i18n("Version %1 — 1.2 GB", backend.pendingVersion)
                    opacity: 0.7
                }
            }

            QQC2.Button {
                text: i18n("Update tonight")
                onClicked: {
                    backend.scheduleUpgrade()
                    root.requestScheduled()
                }
            }

            QQC2.Button {
                text: i18n("Update now")
                highlighted: true
                onClicked: backend.startUpgrade()
            }
        }
    }

    // Description card
    Rectangle {
        Layout.fillWidth: true
        color: Kirigami.Theme.backgroundColor
        border.color: Qt.rgba(Kirigami.Theme.textColor.r,
                              Kirigami.Theme.textColor.g,
                              Kirigami.Theme.textColor.b, 0.12)
        border.width: 1
        radius: Kirigami.Units.largeSpacing
        implicitHeight: descLabel.implicitHeight + Kirigami.Units.gridUnit * 2

        QQC2.Label {
            id: descLabel
            anchors {
                left: parent.left; right: parent.right; top: parent.top
                margins: Kirigami.Units.gridUnit
            }
            text: i18n("This update contains security updates, bug fixes and updated packages for %1.\nAfter downloading, the update will be applied on the next restart.", backend.osName)
            wrapMode: Text.WordWrap
            opacity: 0.85
        }
    }

    // Info rows: installed, auto-update toggle, previous version
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

    // Disclaimer
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
