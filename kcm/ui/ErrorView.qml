import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root
    required property var backend
    property string errorMessage: ""
    signal retry()

    spacing: Kirigami.Units.largeSpacing

    Kirigami.AbstractCard {
        Layout.fillWidth: true
        leftPadding: Kirigami.Units.gridUnit
        rightPadding: Kirigami.Units.gridUnit
        topPadding: Kirigami.Units.gridUnit
        bottomPadding: Kirigami.Units.gridUnit
        background: Rectangle {
            color: Kirigami.Theme.backgroundColor
            border.color: Qt.rgba(Kirigami.Theme.negativeTextColor.r,
                                  Kirigami.Theme.negativeTextColor.g,
                                  Kirigami.Theme.negativeTextColor.b, 0.30)
            border.width: 1
            radius: Kirigami.Units.largeSpacing
        }

        contentItem: RowLayout {
            spacing: Kirigami.Units.largeSpacing

            Rectangle {
                width: 56; height: 56; radius: 28
                color: Qt.rgba(Kirigami.Theme.negativeTextColor.r,
                               Kirigami.Theme.negativeTextColor.g,
                               Kirigami.Theme.negativeTextColor.b, 0.18)
                Kirigami.Icon {
                    anchors.centerIn: parent
                    source: "qrc:/kcm/kcm_software_update/icons/alert-circle.svg"
                    isMask: true
                    width: 32; height: 32
                    color: Kirigami.Theme.negativeTextColor
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                QQC2.Label {
                    text: i18n("An error occurred")
                    font.bold: true
                    font.pointSize: Math.round(Kirigami.Theme.defaultFont.pointSize * 1.05)
                }
                QQC2.Label {
                    text: root.errorMessage || i18n("The update could not be completed.")
                    opacity: 0.7
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        QQC2.Button {
            icon.name: "view-refresh"
            text: i18n("Try again")
            onClicked: root.retry()
        }
    }

    Item { Layout.fillHeight: true }
}
