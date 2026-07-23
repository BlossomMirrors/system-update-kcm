import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root
    required property var backend
    signal dismissed()
    spacing: Kirigami.Units.largeSpacing

    Kirigami.AbstractCard {
        Layout.fillWidth: true
        leftPadding: Kirigami.Units.gridUnit
        rightPadding: Kirigami.Units.gridUnit
        topPadding: Kirigami.Units.gridUnit
        bottomPadding: Kirigami.Units.gridUnit

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.largeSpacing

            RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Rectangle {
                    width: 56; height: 56
                    radius: 28
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
                    Layout.fillWidth: true
                    spacing: 2
                    QQC2.Label {
                        text: i18n("Update ready")
                        font.bold: true
                        font.pointSize: Math.round(Kirigami.Theme.defaultFont.pointSize * 1.05)
                    }
                    QQC2.Label {
                        text: i18n("%1 %2 — Restart required", backend.osName, backend.pendingVersion)
                        opacity: 0.7
                    }
                }
            }

            // Version diff box
            Rectangle {
                Layout.fillWidth: true
                color: Qt.rgba(Kirigami.Theme.textColor.r,
                               Kirigami.Theme.textColor.g,
                               Kirigami.Theme.textColor.b, 0.05)
                radius: Kirigami.Units.smallSpacing
                implicitHeight: versionColumn.implicitHeight

                ColumnLayout {
                    id: versionColumn
                    anchors { left: parent.left; right: parent.right }
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: Kirigami.Units.gridUnit
                        QQC2.Label {
                            text: i18n("Current")
                            opacity: 0.7
                        }
                        Item { Layout.fillWidth: true }
                        QQC2.Label {
                            text: backend.currentVersion
                        }
                    }

                    Kirigami.Separator { Layout.fillWidth: true }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: Kirigami.Units.gridUnit
                        QQC2.Label {
                            text: i18n("After restart")
                            opacity: 0.7
                        }
                        Item { Layout.fillWidth: true }
                        QQC2.Label {
                            text: backend.pendingVersion
                            color: Kirigami.Theme.positiveTextColor
                            font.bold: true
                        }
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }

        QQC2.Button {
            text: i18n("Restart later")
            onClicked: root.dismissed()
        }

        QQC2.Button {
            text: i18n("Restart now")
            highlighted: true
            onClicked: backend.rebootSystem()
        }
    }

    Item { Layout.fillHeight: true }
}
