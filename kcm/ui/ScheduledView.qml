import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root
    required property var backend
    signal cancelled()

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
                color: Qt.rgba(Kirigami.Theme.highlightColor.r,
                               Kirigami.Theme.highlightColor.g,
                               Kirigami.Theme.highlightColor.b, 0.18)
                Kirigami.Icon {
                    anchors.centerIn: parent
                    source: "qrc:/kcm/kcm_software_update/icons/clock.svg"
                    isMask: true
                    width: 32; height: 32
                    color: Kirigami.Theme.highlightColor
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                QQC2.Label {
                    text: i18n("Update scheduled")
                    font.bold: true
                    font.pointSize: Math.round(Kirigami.Theme.defaultFont.pointSize * 1.05)
                }
                QQC2.Label {
                    text: i18n("%1 %2 will be downloaded tonight.", backend.osName, backend.pendingVersion)
                    opacity: 0.7
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            QQC2.Button {
                text: i18n("Cancel")
                palette.buttonText: Kirigami.Theme.negativeTextColor
                onClicked: {
                    backend.cancelUpgrade()
                    root.cancelled()
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
