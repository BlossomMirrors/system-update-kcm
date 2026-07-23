import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

RowLayout {
    id: root
    property string label: ""
    property string value: ""
    property real extraTopMargin: 0
    property alias content: contentLoader.sourceComponent

    Layout.fillWidth: true
    Layout.minimumHeight: Kirigami.Units.gridUnit * 2
    spacing: Kirigami.Units.largeSpacing

    QQC2.Label {
        text: root.label
        font.bold: true
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignVCenter
        Layout.topMargin: root.extraTopMargin
    }

    QQC2.Label {
        text: root.value
        opacity: 0.7
        visible: root.value !== ""
        Layout.alignment: Qt.AlignVCenter
        Layout.topMargin: root.extraTopMargin
    }

    Loader {
        id: contentLoader
        visible: status === Loader.Ready
        Layout.alignment: Qt.AlignVCenter
    }
}
