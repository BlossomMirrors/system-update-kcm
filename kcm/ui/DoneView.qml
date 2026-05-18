import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root
    required property var backend
    spacing: Kirigami.Units.largeSpacing

    property var logLines: []

    ListModel { id: logModel }

    Rectangle {
        Layout.fillWidth: true
        color: Kirigami.Theme.backgroundColor
        border.color: Qt.rgba(Kirigami.Theme.textColor.r,
                              Kirigami.Theme.textColor.g,
                              Kirigami.Theme.textColor.b, 0.12)
        border.width: 1
        radius: Kirigami.Units.largeSpacing
        implicitHeight: doneColumn.implicitHeight + Kirigami.Units.gridUnit * 2

        ColumnLayout {
            id: doneColumn
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
                margins: Kirigami.Units.gridUnit
            }
            spacing: Kirigami.Units.largeSpacing

            RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Rectangle {
                    width: 48; height: 48
                    radius: 24
                    color: Qt.rgba(Kirigami.Theme.positiveTextColor.r,
                                   Kirigami.Theme.positiveTextColor.g,
                                   Kirigami.Theme.positiveTextColor.b, 0.18)
                    Kirigami.Icon {
                        anchors.centerIn: parent
                        source: "checkmark"
                        width: 24; height: 24
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

            // Version diff card
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

            // Collapsible log
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                RowLayout {
                    id: logHeader
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing
                    property bool logExpanded: false

                    Kirigami.Icon {
                        source: logHeader.logExpanded ? "go-down" : "go-next"
                        width: 16; height: 16
                        opacity: 0.7
                    }
                    QQC2.Label {
                        text: i18n("Transaction log")
                        opacity: 0.7
                    }
                    Item { Layout.fillWidth: true }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: logHeader.logExpanded = !logHeader.logExpanded
                    }
                }

                QQC2.ScrollView {
                    Layout.fillWidth: true
                    implicitHeight: 120
                    visible: logHeader.logExpanded
                    ListView {
                        model: logModel
                        delegate: QQC2.Label {
                            width: ListView.view.width
                            text: model.line
                            font.family: "monospace"
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            opacity: 0.8
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
            }
        }
    }

    Item { Layout.fillHeight: true }

    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }

        QQC2.Button {
            text: i18n("Restart later")
        }

        QQC2.Button {
            text: i18n("Restart now")
            highlighted: true
            onClicked: {
                // systemctl reboot via D-Bus would go here
            }
        }
    }
}
