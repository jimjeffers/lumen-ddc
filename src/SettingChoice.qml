import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A setting with a fixed list of values ({value, label} objects), such as the
// color temperature or picture mode.
ColumnLayout {
    id: root
    property string label
    property var options: []
    property int value: -1
    signal chosen(int value)

    visible: options.length > 0
    spacing: 2

    Label { text: root.label; opacity: 0.8 }

    ComboBox {
        id: combo
        Layout.fillWidth: true
        focusPolicy: Qt.NoFocus
        model: root.options
        textRole: "label"
        valueRole: "value"
        currentIndex: indexOfValue(root.value)
        onActivated: root.chosen(currentValue)
        Material.roundedScale: theme.radius
    }
}
