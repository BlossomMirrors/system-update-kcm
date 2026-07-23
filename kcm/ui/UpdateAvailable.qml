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

    // Merged top card: header row + divider + description
    Kirigami.AbstractCard {
        Layout.fillWidth: true
        leftPadding: Kirigami.Units.gridUnit
        rightPadding: Kirigami.Units.gridUnit
        topPadding: Kirigami.Units.gridUnit
        bottomPadding: Kirigami.Units.gridUnit

        contentItem: ColumnLayout {
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing

                Rectangle {
                    width: 56; height: 56; radius: 28
                    color: Qt.rgba(Kirigami.Theme.highlightColor.r,
                                   Kirigami.Theme.highlightColor.g,
                                   Kirigami.Theme.highlightColor.b, 0.18)
                    Kirigami.Icon {
                        anchors.centerIn: parent
                        source: "qrc:/kcm/kcm_software_update/icons/monitor.svg"
                        isMask: true
                        width: 32; height: 32
                        color: Kirigami.Theme.highlightColor
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
                        text: i18n("Version %1", backend.pendingVersion)
                        opacity: 0.7
                    }
                    QQC2.Label {
                        visible: backend.downloadSize !== ""
                        text: i18n("Approximate download size: %1", backend.downloadSize)
                        opacity: 0.7
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                    }
                }

                Item { Layout.fillWidth: true }

                QQC2.Button {
                    text: i18n("Update now")
                    highlighted: true
                    onClicked: backend.startUpgrade()
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.gridUnit
                Layout.bottomMargin: Kirigami.Units.gridUnit
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("This update contains security updates, bug fixes and updated packages for %1. After downloading, the update will be applied on the next restart.", backend.osName)
                wrapMode: Text.WordWrap
                opacity: 0.85
            }
        }
    }

    // What's new: short summary fetched from the changelog feed
    Kirigami.AbstractCard {
        Layout.fillWidth: true
        visible: backend.changelogEntries.length > 0
        leftPadding: Kirigami.Units.gridUnit
        rightPadding: Kirigami.Units.gridUnit
        topPadding: Kirigami.Units.gridUnit
        bottomPadding: Kirigami.Units.gridUnit

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("What's new")
                font.bold: true
            }

            Repeater {
                model: backend.changelogEntries
                delegate: RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing
                    QQC2.Label {
                        text: "•"
                        opacity: 0.7
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: modelData
                        wrapMode: Text.WordWrap
                        opacity: 0.85
                    }
                }
            }

            QQC2.Label {
                Layout.topMargin: Kirigami.Units.smallSpacing
                text: "<a href=\"https://help.blossomos.org/latestchangelog\">"
                    + i18n("Read the full changelog") + "</a>"
                onLinkActivated: link => Qt.openUrlExternally(link)

                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }
            }
        }
    }

    // Info rows: installed, auto-update toggle, previous version
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

    // Disclaimer
    Kirigami.InlineMessage {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.smallSpacing
        visible: true
        type: Kirigami.MessageType.Information
        text: i18n("Updates are applied atomically and can always be rolled back. Your files and settings are never affected.")
    }

    Item { Layout.fillHeight: true }
}
