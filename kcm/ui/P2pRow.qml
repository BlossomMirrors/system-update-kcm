import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root
    required property var p2p

    visible: p2p !== null && p2p.available
    Layout.fillWidth: true
    spacing: Kirigami.Units.smallSpacing

    Kirigami.Separator { Layout.fillWidth: true }

    InfoRow {
        label: i18n("Share updates on local network")
        content: RowLayout {
            QQC2.Label {
                text: root.p2p.enabled ? i18n("On") : i18n("Off")
                opacity: 0.7
            }
            QQC2.Switch {
                enabled: !root.p2p.locked && !root.p2p.busy
                onToggled: root.p2p.setEnabled(checked)
                Binding on checked { value: root.p2p.enabled }
            }
        }
    }

    QQC2.Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        opacity: 0.7
        font: Kirigami.Theme.smallFont
        text: root.p2p.locked
              ? i18n("This setting is managed by your administrator.")
              : i18n("Downloads updates from other computers on this network and shares them in return. This uses several gigabytes of storage.")
    }

    QQC2.Label {
        Layout.fillWidth: true
        visible: root.p2p.enabled
        wrapMode: Text.WordWrap
        opacity: 0.7
        font: Kirigami.Theme.smallFont
        text: i18n("Other computers found: %1", root.p2p.peerCount) + "  ·  " + i18n("Storage used: %1", root.p2p.storeSize)
    }
}
