import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

RowLayout {
    id: root
    property string label: ""
    property string value: ""
    property alias content: contentLoader.sourceComponent

    Layout.fillWidth: true
    Layout.margins: Kirigami.Units.gridUnit
    spacing: Kirigami.Units.largeSpacing

    QQC2.Label {
        text: root.label
        font.bold: true
        Layout.fillWidth: true
    }

    QQC2.Label {
        text: root.value
        opacity: 0.7
        visible: root.value !== ""
    }

    Loader {
        id: contentLoader
        visible: status === Loader.Ready
    }
}
