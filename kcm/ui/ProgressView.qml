import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root
    required property var backend
    spacing: Kirigami.Units.largeSpacing

    property var logLines: []

    Connections {
        target: backend
        function onProgressMessageChanged() {
            if (backend.progressMessage !== "") {
                logLines.push(backend.progressMessage)
                logModel.append({ line: backend.progressMessage })
            }
        }
    }

    ListModel { id: logModel }

    Rectangle {
        Layout.fillWidth: true
        color: Kirigami.Theme.backgroundColor
        border.color: Qt.rgba(Kirigami.Theme.textColor.r,
                              Kirigami.Theme.textColor.g,
                              Kirigami.Theme.textColor.b, 0.12)
        border.width: 1
        radius: Kirigami.Units.largeSpacing
        implicitHeight: mainColumn.implicitHeight + Kirigami.Units.gridUnit * 2

        ColumnLayout {
            id: mainColumn
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
                margins: Kirigami.Units.gridUnit
            }
            spacing: Kirigami.Units.largeSpacing

            // Header row: icon + title/subtitle
            RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Rectangle {
                    width: 56; height: 56
                    radius: 28
                    color: Qt.rgba(Kirigami.Theme.highlightColor.r,
                                   Kirigami.Theme.highlightColor.g,
                                   Kirigami.Theme.highlightColor.b, 0.18)
                    Kirigami.Icon {
                        anchors.centerIn: parent
                        source: "qrc:/kcm/kcm_software_update/icons/download.svg"
                        isMask: true
                        width: 32; height: 32
                        color: Kirigami.Theme.highlightColor
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    QQC2.Label {
                        text: i18n("%1 %2 is being downloaded", backend.osName, backend.pendingVersion)
                        font.bold: true
                        font.pointSize: Math.round(Kirigami.Theme.defaultFont.pointSize * 1.05)
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    QQC2.Label {
                        text: backend.progressMessage || i18n("Downloading update…")
                        opacity: 0.7
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }

            // Progress bar
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                QQC2.ProgressBar {
                    Layout.fillWidth: true
                    value: backend.progressPercent / 100.0
                    implicitHeight: 4
                }

                RowLayout {
                    Layout.fillWidth: true
                    QQC2.Label {
                        text: backend.progressPercent + "%"
                        opacity: 0.7
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                    }
                    Item { Layout.fillWidth: true }
                    QQC2.Label {
                        text: "1.2 GB"
                        opacity: 0.7
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                    }
                }
            }

            // Collapsible transaction log
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                RowLayout {
                    id: logHeader
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing
                    property bool logExpanded: false

                    TapHandler {
                        cursorShape: Qt.PointingHandCursor
                        onTapped: logHeader.logExpanded = !logHeader.logExpanded
                    }

                    Kirigami.Icon {
                        source: logHeader.logExpanded
                            ? "qrc:/kcm/kcm_software_update/icons/chevron-down.svg"
                            : "qrc:/kcm/kcm_software_update/icons/chevron-right.svg"
                        isMask: true
                        width: 14; height: 14
                        color: Kirigami.Theme.textColor
                        opacity: 0.7
                    }

                    QQC2.Label {
                        text: i18n("Transaction log")
                        opacity: 0.7
                        font.pointSize: Kirigami.Theme.defaultFont.pointSize
                    }

                    Item { Layout.fillWidth: true }
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

    // Cancel button row
    RowLayout {
        Layout.fillWidth: true

        Item { Layout.fillWidth: true }

        QQC2.Button {
            id: cancelBtn
            QQC2.ToolTip.text: i18n("Cancel the update")
            palette.button: Kirigami.Theme.negativeTextColor
            onClicked: backend.cancelUpgrade()
            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing
                Kirigami.Icon {
                    source: "qrc:/kcm/kcm_software_update/icons/x.svg"
                    isMask: true
                    color: cancelBtn.palette.buttonText
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                }
                QQC2.Label {
                    text: i18n("Cancel")
                    color: cancelBtn.palette.buttonText
                }
            }
        }
    }
}
