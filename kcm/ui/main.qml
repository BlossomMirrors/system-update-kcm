import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCMUtils

KCMUtils.SimpleKCM {
    id: root

    implicitWidth: Kirigami.Units.gridUnit * 38
    implicitHeight: Kirigami.Units.gridUnit * 30

    // Backend is created in C++ and exposed via the KCM object
    readonly property var backend: kcm.backend

    property string errorMsg: ""

    Connections {
        target: root.backend
        function onUpgradeFinished(success) {
            if (success) {
                mainLoader.state = "done"
            } else {
                root.errorMsg = root.backend.progressMessage
                mainLoader.state = "error"
            }
        }
        function onRollbackFinished(success) {
            if (success) {
                mainLoader.state = "rollbackDone"
            }
        }
        function onErrorOccurred(message) {
            root.errorMsg = message
            mainLoader.state = "error"
        }
    }

    Kirigami.PromptDialog {
        id: resetDialog
        title: i18n("Remove layered packages?")
        subtitle: i18n("The following packages will be removed and the system will restart afterwards:\n\n%1",
                       root.backend ? root.backend.layeredPackages.join("\n") : "")
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Remove packages and restart")
                icon.name: "edit-delete"
                onTriggered: {
                    resetDialog.close()
                    root.backend.startReset()
                }
            }
        ]
    }

    Kirigami.PromptDialog {
        id: rollbackDialog
        title: i18n("Roll back to previous version?")
        subtitle: root.backend
            ? i18n("The system will then restart with %1 %2.",
                   root.backend.osName, root.backend.previousVersion) + "\n\n"
              + i18n("Your files and settings stay unchanged. You can always update to the latest version again afterwards.")
            : ""
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Roll back and restart")
                icon.name: "edit-undo"
                onTriggered: {
                    rollbackDialog.close()
                    root.backend.startRollback()
                }
            }
        ]
    }

    ColumnLayout {
        anchors {
            fill: parent
            margins: 20
        }
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root.backend && root.backend.layeredPackages.length > 0
                     && (mainLoader.effectiveView === "upToDate"
                         || mainLoader.effectiveView === "updateAvailable")
            type: Kirigami.MessageType.Error
            text: "<b>" + i18n("System updates are paused") + "</b><br>"
                + i18n("Packages layered with rpm-ostree prevent updates: %1",
                       root.backend ? root.backend.layeredPackages.join(", ") : "")

            actions: [
                Kirigami.Action {
                    text: i18n("Remove packages…")
                    icon.name: "edit-delete"
                    onTriggered: resetDialog.open()
                }
            ]
        }

        // Drives view transitions via state machine so we never push/pop on SimpleKCM
        Item {
            id: mainLoader
            Layout.fillWidth: true
            Layout.fillHeight: true
            state: "auto"

            states: [
                State { name: "auto" },
                State { name: "done" },
                State { name: "rollbackDone" },
                State { name: "error" },
                State { name: "scheduled" }
            ]

            // Compute effective state from backend + explicit overrides
            property string effectiveView: {
                if (state === "done") return "done"
                if (state === "rollbackDone") return "rollbackDone"
                if (state === "error") return "error"
                if (state === "scheduled") return "scheduled"
                if (root.backend && root.backend.busy) return "progress"
                if (root.backend && root.backend.updateAvailable) return "updateAvailable"
                return "upToDate"
            }

            Loader {
                anchors.fill: parent
                sourceComponent: {
                    switch (mainLoader.effectiveView) {
                    case "progress":        return progressComp
                    case "done":            return doneComp
                    case "rollbackDone":    return rollbackDoneComp
                    case "error":           return errorComp
                    case "scheduled":       return scheduledComp
                    case "updateAvailable": return updateAvailableComp
                    default:                return upToDateComp
                    }
                }
            }
        }
    }

    Component {
        id: updateAvailableComp
        UpdateAvailable {
            backend: root.backend
            onRequestRollbackConfirm: rollbackDialog.open()
            onRequestScheduled: mainLoader.state = "scheduled"
        }
    }

    Component {
        id: upToDateComp
        UpToDateView {
            backend: root.backend
            onRequestRollbackConfirm: rollbackDialog.open()
        }
    }

    Component {
        id: progressComp
        ProgressView { backend: root.backend }
    }

    Component {
        id: doneComp
        DoneView {
            backend: root.backend
            onDismissed: mainLoader.state = "auto"
        }
    }

    Component {
        id: rollbackDoneComp
        RollbackDone {
            backend: root.backend
            onDismissed: mainLoader.state = "auto"
        }
    }

    Component {
        id: errorComp
        ErrorView {
            backend: root.backend
            errorMessage: root.errorMsg
            onRetry: {
                mainLoader.state = "auto"
                root.backend.startUpgrade()
            }
        }
    }

    Component {
        id: scheduledComp
        ScheduledView {
            backend: root.backend
            onCancelled: mainLoader.state = "auto"
        }
    }
}
