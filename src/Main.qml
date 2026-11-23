import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: Math.max(560, 420 * Math.max(1, backend.monitors.length) + 48)
    height: 720
    minimumWidth: 480
    minimumHeight: 560
    visible: true
    title: "Lumen"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    property int selected: 0
    readonly property int count: backend.monitors.length

    onCountChanged: selected = Math.min(selected, Math.max(0, count - 1))

    function nudge(id, delta) { backend.step(selected, id, delta) }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Esc"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = false }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; onActivated: backend.linked = !backend.linked }
    Shortcut { sequence: "L"; context: Qt.ApplicationShortcut; onActivated: backend.linked = !backend.linked }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; onActivated: backend.refresh() }
    Shortcut { sequences: ["Left", "H"]; context: Qt.ApplicationShortcut; onActivated: win.selected = Math.max(0, win.selected - 1) }
    Shortcut { sequences: ["Right", "Tab"]; context: Qt.ApplicationShortcut; onActivated: win.selected = Math.min(win.count - 1, win.selected + 1) }
    Shortcut { sequences: ["Up", "K"]; context: Qt.ApplicationShortcut; onActivated: win.nudge("brightness", 5) }
    Shortcut { sequences: ["Down", "J"]; context: Qt.ApplicationShortcut; onActivated: win.nudge("brightness", -5) }
    Shortcut { sequence: "Shift+Up"; context: Qt.ApplicationShortcut; onActivated: win.nudge("contrast", 5) }
    Shortcut { sequence: "Shift+Down"; context: Qt.ApplicationShortcut; onActivated: win.nudge("contrast", -5) }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 16

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Label { text: "Lumen"; font.pixelSize: 26; font.bold: true }
                Item { Layout.fillWidth: true }
                Switch {
                    text: qsTr("Link monitors")
                    checked: backend.linked
                    focusPolicy: Qt.NoFocus
                    onToggled: backend.linked = checked
                    visible: win.count > 1
                }
                Button {
                    text: qsTr("Refresh")
                    flat: true
                    focusPolicy: Qt.NoFocus
                    onClicked: backend.refresh()
                }
            }

            Label {
                visible: backend.error !== ""
                text: backend.error
                color: "#ff6b6b"
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 16
                Repeater {
                    model: backend.monitors
                    MonitorCard {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1
                        monitor: modelData
                        index: model.index
                        count: win.count
                        selected: win.selected === model.index
                        onPicked: win.selected = model.index
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                opacity: 0.45
                font.pixelSize: 12
                text: backend.notice !== "" ? backend.notice
                    : backend.linked && win.count > 1 ? qsTr("Changes apply to every monitor · ? for shortcuts")
                    : qsTr("? for shortcuts")
            }
        }

        Rectangle {
            anchors.fill: parent
            visible: win.helpVisible
            color: "#cc0e0e10"
            MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 440)
                height: help.implicitHeight + 48
                radius: theme.radius
                color: "#1c1c20"
                border.color: "#2a2a2e"

                GridLayout {
                    id: help
                    anchors.fill: parent
                    anchors.margins: 24
                    columns: 2
                    columnSpacing: 24
                    rowSpacing: 8

                    Repeater {
                        model: [
                            ["↑ / ↓", qsTr("Brightness up / down")],
                            ["Shift + ↑ / ↓", qsTr("Contrast up / down")],
                            ["← / →", qsTr("Select monitor")],
                            ["Space / L", qsTr("Link or unlink monitors")],
                            ["R", qsTr("Reread settings")],
                            ["?", qsTr("Show or hide shortcuts")],
                            ["Q", qsTr("Quit")]
                        ]
                        delegate: Item {
                            Layout.columnSpan: 2
                            Layout.fillWidth: true
                            implicitHeight: row.implicitHeight
                            RowLayout {
                                id: row
                                anchors.fill: parent
                                Label { text: modelData[0]; font.bold: true; color: theme.accent; Layout.preferredWidth: 130 }
                                Label { text: modelData[1]; Layout.fillWidth: true; wrapMode: Text.Wrap }
                            }
                        }
                    }
                }
            }
        }
    }
}
