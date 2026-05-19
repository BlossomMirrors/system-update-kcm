import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root
    required property var backend
    signal accepted()
    signal rejected()

    spacing: Kirigami.Units.largeSpacing
    implicitWidth: Kirigami.Units.gridUnit * 32

    Rectangle {
        Layout.fillWidth: true
        color: Kirigami.Theme.backgroundColor
        border.color: Qt.rgba(Kirigami.Theme.textColor.r,
                              Kirigami.Theme.textColor.g,
                              Kirigami.Theme.textColor.b, 0.12)
        border.width: 1
        radius: Kirigami.Units.largeSpacing
        implicitHeight: confirmColumn.implicitHeight + Kirigami.Units.gridUnit * 2

        ColumnLayout {
            id: confirmColumn
            anchors {
                left: parent.left; right: parent.right; top: parent.top
                margins: Kirigami.Units.gridUnit
            }
            spacing: Kirigami.Units.largeSpacing

            RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Rectangle {
                    width: 56; height: 56
                    radius: 28
                    color: Qt.rgba(Kirigami.Theme.neutralTextColor.r,
                                   Kirigami.Theme.neutralTextColor.g,
                                   Kirigami.Theme.neutralTextColor.b, 0.18)
                    Kirigami.Icon {
                        anchors.centerIn: parent
                        source: "qrc:/kcm/kcm_software_update/icons/rotate-ccw.svg"
                        isMask: true
                        width: 32; height: 32
                        color: Kirigami.Theme.neutralTextColor
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    QQC2.Label {
                        text: i18n("Roll back to previous version?")
                        font.bold: true
                        font.pointSize: Math.round(Kirigami.Theme.defaultFont.pointSize * 1.05)
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    QQC2.Label {
                        text: i18n("The system will then restart with %1 %2.",
                                   backend.osName, backend.previousVersion)
                        opacity: 0.7
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }

            // Version diff
            Rectangle {
                Layout.fillWidth: true
                color: Qt.rgba(Kirigami.Theme.textColor.r,
                               Kirigami.Theme.textColor.g,
                               Kirigami.Theme.textColor.b, 0.05)
                radius: Kirigami.Units.smallSpacing
                implicitHeight: rbVersionColumn.implicitHeight

                ColumnLayout {
                    id: rbVersionColumn
                    anchors { left: parent.left; right: parent.right }
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: Kirigami.Units.gridUnit
                        QQC2.Label { text: i18n("Current"); opacity: 0.7 }
                        Item { Layout.fillWidth: true }
                        QQC2.Label { text: backend.currentVersion }
                    }

                    Kirigami.Separator { Layout.fillWidth: true }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: Kirigami.Units.gridUnit
                        QQC2.Label { text: i18n("After restart"); opacity: 0.7 }
                        Item { Layout.fillWidth: true }
                        QQC2.Label {
                            text: backend.previousVersion
                            color: Kirigami.Theme.neutralTextColor
                            font.bold: true
                        }
                    }
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("Your files and settings stay unchanged. You can always update to the latest version again afterwards.")
                wrapMode: Text.WordWrap
                opacity: 0.75
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }

        QQC2.Button {
            text: i18n("Cancel")
            onClicked: root.rejected()
        }

        QQC2.Button {
            text: i18n("Roll back and restart")
            palette.buttonText: Kirigami.Theme.neutralTextColor
            onClicked: root.accepted()
        }
    }
}
