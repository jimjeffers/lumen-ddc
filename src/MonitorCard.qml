import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Every control for one monitor, as its profile lists them. Changes go
// through backend.setControl, which applies them to all monitors when
// they're linked.
Rectangle {
    id: card
    property var monitor
    property int index: 0
    property int count: 1
    property bool selected: false
    signal picked()

    color: "#17171a"
    radius: theme.radius
    border.width: selected ? 2 : 1
    border.color: selected ? theme.accent : "#2a2a2e"

    readonly property string position: count === 1 ? qsTr("Monitor")
        : index === 0 ? qsTr("Left")
        : index === count - 1 ? qsTr("Right")
        : qsTr("Middle")

    MouseArea { anchors.fill: parent; onPressed: (mouse) => { card.picked(); mouse.accepted = false } }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 2
                Layout.fillWidth: true
                Label {
                    text: card.position
                    font.pixelSize: 13
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 1
                    color: card.selected ? theme.accent : "#8a8a90"
                }
                Label { text: card.monitor.model; font.pixelSize: 20; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                Label {
                    text: qsTr("%1 · %2").arg(card.monitor.serial).arg(card.monitor.connector)
                    opacity: 0.55
                    font.pixelSize: 12
                }
            }
            BusyIndicator {
                running: card.monitor.busy
                visible: running
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
            }
        }

        Label {
            visible: card.monitor.error !== ""
            text: card.monitor.error
            color: "#ff6b6b"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Label {
            visible: !card.monitor.ready && card.monitor.error === ""
            text: qsTr("Reading settings…")
            opacity: 0.6
        }

        // A model without a profile gets the standard controls and a pointer
        // to adding one.
        ColumnLayout {
            visible: !card.monitor.supported
            Layout.fillWidth: true
            spacing: 4
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.75
                text: qsTr("Lumen has no profile for this model yet, so only standard controls are shown.")
            }
            RowLayout {
                spacing: 4
                Button {
                    text: qsTr("How to add it")
                    flat: true
                    focusPolicy: Qt.NoFocus
                    onClicked: Qt.openUrlExternally(backend.contributeUrl)
                }
                Button {
                    text: qsTr("Start a profile")
                    flat: true
                    focusPolicy: Qt.NoFocus
                    onClicked: Qt.openUrlExternally("file://" + backend.profileFile(card.index))
                }
            }
        }

        Repeater {
            model: card.monitor.controls

            Loader {
                required property var modelData
                Layout.fillWidth: true
                sourceComponent: modelData.type === "slider" ? sliderControl
                    : modelData.type === "toggle" ? toggleControl
                    : choiceControl

                Component {
                    id: sliderControl
                    SettingSlider {
                        label: modelData.label
                        value: modelData.value
                        maximum: modelData.max
                        onMoved: (v) => backend.setControl(card.index, modelData.id, v)
                    }
                }
                Component {
                    id: choiceControl
                    SettingChoice {
                        label: modelData.label
                        options: modelData.options
                        value: modelData.value
                        onChosen: (v) => backend.setControl(card.index, modelData.id, v)
                    }
                }
                Component {
                    id: toggleControl
                    Switch {
                        text: modelData.label
                        checked: modelData.value === modelData.on
                        focusPolicy: Qt.NoFocus
                        onToggled: backend.setControl(card.index, modelData.id, checked ? modelData.on : modelData.off)
                        ToolTip.visible: hovered && modelData.note !== ""
                        ToolTip.text: modelData.note
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }

        ToolButton {
            visible: card.monitor.supported
            Layout.alignment: Qt.AlignRight
            text: qsTr("Edit profile…")
            focusPolicy: Qt.NoFocus
            onClicked: Qt.openUrlExternally("file://" + backend.profileFile(card.index))
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Open a copy of this model's profile to adjust")
        }
    }
}
