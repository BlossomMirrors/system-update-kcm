import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root
    required property var backend
    spacing: Kirigami.Units.largeSpacing

    Rectangle {
        Layout.fillWidth: true
        color: Kirigami.Theme.backgroundColor
        border.color: Qt.rgba(Kirigami.Theme.textColor.r,
                              Kirigami.Theme.textColor.g,
                              Kirigami.Theme.textColor.b, 0.12)
        border.width: 1
        radius: Kirigami.Units.largeSpacing
        implicitHeight: rbDoneRow.implicitHeight + Kirigami.Units.gridUnit * 2

        RowLayout {
            id: rbDoneRow
            anchors {
                left: parent.left; right: parent.right; top: parent.top
                margins: Kirigami.Units.gridUnit
            }
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
                    text: i18n("Rollback ready")
                    font.bold: true
                    font.pointSize: Math.round(Kirigami.Theme.defaultFont.pointSize * 1.05)
                }
                QQC2.Label {
                    text: i18n("After the restart the system will run version %1.",
                               backend.previousVersion)
                    opacity: 0.7
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
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
            palette.buttonText: Kirigami.Theme.neutralTextColor
            highlighted: true
            onClicked: { /* systemctl reboot */ }
        }
    }
}
