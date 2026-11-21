import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One continuous setting: a label, a slider, and the current number. The
// slider follows the monitor except while it's being dragged, so a slow DDC
// reply can't yank the handle from under the pointer.
ColumnLayout {
    id: root
    property string label
    property int value: -1
    property int maximum: 100
    signal moved(int value)

    visible: value >= 0
    spacing: 0

    RowLayout {
        Layout.fillWidth: true
        Label { text: root.label; opacity: 0.8; Layout.fillWidth: true }
        Label {
            text: Math.round(slider.value).toLocaleString(Qt.locale(), 'f', 0)
            font.bold: true
            font.features: { "tnum": 1 }
        }
    }

    Slider {
        id: slider
        Layout.fillWidth: true
        from: 0
        to: root.maximum
        stepSize: 1
        focusPolicy: Qt.NoFocus
        onMoved: root.moved(Math.round(value))

        Binding on value {
            value: root.value
            when: !slider.pressed
            restoreMode: Binding.RestoreNone
        }
    }
}
