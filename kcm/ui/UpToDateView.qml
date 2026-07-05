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
        implicitHeight: headerCol.implicitHeight + Kirigami.Units.gridUnit * 2

        ColumnLayout {
            id: headerCol
            anchors {
                left: parent.left; right: parent.right; top: parent.top
                margins: Kirigami.Units.gridUnit
            }
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
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
                    id: checkBtn
                    enabled: !backend.checking
                    leftPadding: Kirigami.Units.largeSpacing
                    rightPadding: Kirigami.Units.largeSpacing
                    onClicked: backend.checkForUpdates()
                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing
                        QQC2.BusyIndicator {
                            visible: backend.checking
                            running: backend.checking
                            padding: 0
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        }
                        Kirigami.Icon {
                            visible: !backend.checking
                            source: "qrc:/kcm/kcm_software_update/icons/refresh-cw.svg"
                            isMask: true
                            color: checkBtn.palette.buttonText
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        }
                        QQC2.Label {
                            text: backend.checking ? i18n("Checking…") : i18n("Check for updates")
                            color: checkBtn.palette.buttonText
                        }
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
        color: Qt.rgba(Kirigami.Theme.textColor.r,
                       Kirigami.Theme.textColor.g,
                       Kirigami.Theme.textColor.b, 0.05)
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
