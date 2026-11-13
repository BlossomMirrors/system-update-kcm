import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root
    required property var backend
    spacing: Kirigami.Units.largeSpacing

    // Seeded from the backend so reopening mid upgrade shows the full log
    function reloadLog() {
        logModel.clear()
        for (const line of backend.progressLog)
            logModel.append({ line: line })
    }

    Component.onCompleted: reloadLog()

    Connections {
        target: backend
        function onProgressLogReset() { root.reloadLog() }
        function onProgressLogAppended(line) { logModel.append({ line: line }) }
    }

    ListModel { id: logModel }

    Kirigami.AbstractCard {
        Layout.fillWidth: true
        leftPadding: Kirigami.Units.gridUnit
        rightPadding: Kirigami.Units.gridUnit
        topPadding: Kirigami.Units.gridUnit
        bottomPadding: Kirigami.Units.gridUnit

        contentItem: ColumnLayout {
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
                        source: backend.resetting
                            ? "qrc:/kcm/kcm_software_update/icons/rotate-ccw.svg"
                            : "qrc:/kcm/kcm_software_update/icons/download.svg"
                        isMask: true
                        width: 32; height: 32
                        color: Kirigami.Theme.highlightColor
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    QQC2.Label {
                        text: backend.resetting
                            ? i18n("Layered packages are being removed")
                            : backend.applying
                              ? i18n("%1 %2 is being installed", backend.osName, backend.pendingVersion)
                              : i18n("%1 %2 is being downloaded", backend.osName, backend.pendingVersion)
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
                    indeterminate: !backend.hasPercent
                    value: backend.progressPercent / 100.0
                    implicitHeight: 4
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: !backend.resetting
                    QQC2.Label {
                        visible: backend.hasPercent
                        text: backend.progressPercent + "%"
                        opacity: 0.7
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                    }
                    QQC2.Label {
                        visible: backend.timeRemaining !== ""
                        text: (backend.hasPercent ? "· " : "") + backend.timeRemaining
                        opacity: 0.7
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                    }
                    Item { Layout.fillWidth: true }
                    QQC2.Label {
                        visible: backend.downloadedSize !== ""
                        text: i18n("%1 of %2", backend.downloadedSize, backend.downloadSize)
                        opacity: 0.7
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                    }
                }
            }

            // Collapsible transaction log
            ColumnLayout {
                Layout.fillWidth: true
                visible: !backend.resetting
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
                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
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
                        clip: true
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

    QQC2.Label {
        Layout.fillWidth: true
        visible: !backend.resetting
        text: i18n("You can close System Settings. The update continues in the background and the restart is up to you.")
        wrapMode: Text.WordWrap
        opacity: 0.7
    }

    Item { Layout.fillHeight: true }

    // Cancel button row
    RowLayout {
        Layout.fillWidth: true

        Item { Layout.fillWidth: true }

        QQC2.Button {
            icon.name: "dialog-cancel"
            text: backend.cancelling ? i18n("Cancelling…") : i18n("Cancel")
            enabled: !backend.cancelling
            onClicked: backend.cancelUpgrade()
        }
    }
}
