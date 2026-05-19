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

    // Drives view transitions via state machine so we never push/pop on SimpleKCM
    Item {
        id: mainLoader
        anchors {
            fill: parent
            margins: 20
        }
        state: "auto"

        states: [
            State { name: "auto" },
            State { name: "done" },
            State { name: "rollbackDone" },
            State { name: "error" },
            State { name: "rollbackConfirm" },
            State { name: "scheduled" }
        ]

        // Compute effective state from backend + explicit overrides
        property string effectiveView: {
            if (state === "done") return "done"
            if (state === "rollbackDone") return "rollbackDone"
            if (state === "error") return "error"
            if (state === "rollbackConfirm") return "rollbackConfirm"
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
                case "rollbackConfirm": return rollbackConfirmComp
                case "scheduled":       return scheduledComp
                case "updateAvailable": return updateAvailableComp
                default:                return upToDateComp
                }
            }
        }
    }

    Component {
        id: updateAvailableComp
        UpdateAvailable {
            backend: root.backend
            onRequestRollbackConfirm: mainLoader.state = "rollbackConfirm"
            onRequestScheduled: mainLoader.state = "scheduled"
        }
    }

    Component {
        id: upToDateComp
        UpToDateView {
            backend: root.backend
            onRequestRollbackConfirm: mainLoader.state = "rollbackConfirm"
        }
    }

    Component {
        id: progressComp
        ProgressView { backend: root.backend }
    }

    Component {
        id: doneComp
        DoneView { backend: root.backend }
    }

    Component {
        id: rollbackDoneComp
        RollbackDone { backend: root.backend }
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
        id: rollbackConfirmComp
        RollbackConfirm {
            backend: root.backend
            onAccepted: {
                mainLoader.state = "auto"
                root.backend.startRollback()
            }
            onRejected: mainLoader.state = "auto"
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
