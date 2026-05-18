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

    Rectangle {
        Layout.fillWidth: true
        color: Kirigami.Theme.backgroundColor
        border.color: Qt.rgba(Kirigami.Theme.negativeTextColor.r,
                              Kirigami.Theme.negativeTextColor.g,
                              Kirigami.Theme.negativeTextColor.b, 0.30)
        border.width: 1
        radius: Kirigami.Units.largeSpacing
        implicitHeight: errCol.implicitHeight + Kirigami.Units.gridUnit * 2

        ColumnLayout {
            id: errCol
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: Kirigami.Units.gridUnit }
            spacing: Kirigami.Units.largeSpacing

            RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Rectangle {
                    width: 48; height: 48; radius: 24
                    color: Qt.rgba(Kirigami.Theme.negativeTextColor.r,
                                   Kirigami.Theme.negativeTextColor.g,
                                   Kirigami.Theme.negativeTextColor.b, 0.18)
                    Kirigami.Icon {
                        anchors.centerIn: parent
                        source: "dialog-error"
                        width: 24; height: 24
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
    }

    Item { Layout.fillHeight: true }

    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        QQC2.Button {
            text: i18n("Try again")
            icon.name: "view-refresh"
            onClicked: root.retry()
        }
    }
}
