import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Every setting for one monitor. Changes go through backend.setValue, which
// applies them to all monitors when they're linked.
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

        SettingSlider {
            Layout.fillWidth: true
            label: qsTr("Brightness")
            value: card.monitor.brightness
            maximum: card.monitor.brightnessMax
            onMoved: (v) => backend.setValue(card.index, 0x10, v)
        }

        SettingSlider {
            Layout.fillWidth: true
            label: qsTr("Contrast")
            value: card.monitor.contrast
            maximum: card.monitor.contrastMax
            onMoved: (v) => backend.setValue(card.index, 0x12, v)
        }

        SettingChoice {
            Layout.fillWidth: true
            label: qsTr("Color temperature")
            options: card.monitor.colorPresets
            value: card.monitor.colorPreset
            onChosen: (v) => backend.setValue(card.index, 0x14, v)
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            SettingChoice {
                Layout.fillWidth: true
                label: qsTr("Picture mode")
                options: card.monitor.modes
                value: card.monitor.mode
                onChosen: (v) => backend.setValue(card.index, 0xdc, v)
            }
            ToolButton {
                visible: card.monitor.modes.length > 0
                Layout.alignment: Qt.AlignBottom
                text: qsTr("Names…")
                focusPolicy: Qt.NoFocus
                onClicked: Qt.openUrlExternally("file://" + backend.codesFile(card.index))
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Name this model's picture modes and backlight")
            }
        }

        SettingChoice {
            Layout.fillWidth: true
            visible: card.monitor.haloAvailable && options.length > 0
            label: qsTr("Backlight (MoonHalo)")
            options: card.monitor.haloOptions
            value: card.monitor.halo
            onChosen: (v) => backend.setValue(card.index, card.monitor.haloCode, v)
        }

        SettingSlider {
            Layout.fillWidth: true
            label: qsTr("Speaker volume")
            value: card.monitor.volume
            maximum: card.monitor.volumeMax
            onMoved: (v) => backend.setValue(card.index, 0x62, v)
        }

        Item { Layout.fillHeight: true }
    }
}
