import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root
    modal: true
    anchors.centerIn: parent
    width: 460
    height: col.implicitHeight + 40
    padding: 20
    background: Rectangle { color: bgColor; radius: 10; border.color: accentColor; border.width: 1 }

    property color textColor
    property color subColor
    property color cardColor
    property color accentColor
    property color bgColor

    property int    _profileIdx: 0
    property string _button: ""

    // mode: 0=rebind, 1=macro
    property int mode: 0

    // rebind
    property string rebindValue: ""

    // macro fields
    property string macroTrigger: "press"
    property string macroHold: "once"
    property string macroSequence: ""

    function open(profileIdx, button, currentBinding) {
        _profileIdx = profileIdx
        _button = button
        // detect macro
        if (currentBinding.startsWith("macro:")) {
            mode = 1
            var parts = currentBinding.split(":")
            macroTrigger = parts[1] || "press"
            macroHold    = parts[2] || "once"
            macroSequence = currentBinding.split(parts[2] + ":").slice(1).join(parts[2] + ":").trim()
        } else {
            mode = 0
            rebindValue = currentBinding
        }
        visible = true
    }

    function buildValue() {
        if (mode === 0) return rebindValue.trim()
        return "macro:" + macroTrigger + ":" + macroHold + ": " + macroSequence.trim()
    }

    function apply() {
        config.setBinding(_profileIdx, _button, buildValue())
        visible = false
    }

    ColumnLayout {
        id: col
        width: parent.width
        spacing: 14

        Text {
            text: "Edit: " + root._button
            color: root.textColor
            font.pixelSize: 16
            font.bold: true
        }

        // mode toggle
        RowLayout {
            spacing: 8
            Repeater {
                model: ["Rebind", "Macro"]
                delegate: Rectangle {
                    width: 90; height: 30; radius: 6
                    color: root.mode === index ? root.accentColor : root.cardColor
                    Text { anchors.centerIn: parent; text: modelData; color: root.textColor; font.pixelSize: 12 }
                    MouseArea { anchors.fill: parent; onClicked: root.mode = index }
                }
            }
        }

        // ── REBIND ──
        ColumnLayout {
            visible: root.mode === 0
            Layout.fillWidth: true
            spacing: 6
            Text { text: "Key / Button"; color: root.subColor; font.pixelSize: 11 }
            Rectangle {
                Layout.fillWidth: true; height: 34; radius: 6; color: root.cardColor
                TextInput {
                    anchors.fill: parent; anchors.margins: 8
                    text: root.rebindValue
                    color: root.textColor; font.pixelSize: 13; selectByMouse: true
                    onTextChanged: root.rebindValue = text
                }
            }
            Text {
                text: "e.g. F13  |  LEFTCTRL  |  BTN_LEFT  |  NONE"
                color: root.subColor; font.pixelSize: 10
            }
        }

        // ── MACRO ──
        ColumnLayout {
            visible: root.mode === 1
            Layout.fillWidth: true
            spacing: 10

            // trigger
            ColumnLayout {
                spacing: 4
                Text { text: "Trigger"; color: root.subColor; font.pixelSize: 11 }
                RowLayout {
                    spacing: 6
                    Repeater {
                        model: ["press", "release"]
                        delegate: Rectangle {
                            width: 80; height: 28; radius: 6
                            color: root.macroTrigger === modelData ? root.accentColor : root.cardColor
                            Text { anchors.centerIn: parent; text: modelData; color: root.textColor; font.pixelSize: 12 }
                            MouseArea { anchors.fill: parent; onClicked: root.macroTrigger = modelData }
                        }
                    }
                }
            }

            // hold
            ColumnLayout {
                spacing: 4
                Text { text: "While Held"; color: root.subColor; font.pixelSize: 11 }
                RowLayout {
                    spacing: 6
                    Repeater {
                        model: ["once", "repeat", "toggle"]
                        delegate: Rectangle {
                            width: 80; height: 28; radius: 6
                            color: root.macroHold === modelData ? root.accentColor : root.cardColor
                            Text { anchors.centerIn: parent; text: modelData; color: root.textColor; font.pixelSize: 12 }
                            MouseArea { anchors.fill: parent; onClicked: root.macroHold = modelData }
                        }
                    }
                }
            }

            // sequence
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text { text: "Sequence (comma separated)"; color: root.subColor; font.pixelSize: 11 }
                Rectangle {
                    Layout.fillWidth: true; height: 34; radius: 6; color: root.cardColor
                    TextInput {
                        anchors.fill: parent; anchors.margins: 8
                        text: root.macroSequence
                        color: root.textColor; font.pixelSize: 12; selectByMouse: true
                        onTextChanged: root.macroSequence = text
                    }
                }
                Text {
                    text: "e.g.  LEFTCTRL+C, 50ms, LEFTCTRL+V"
                    color: root.subColor; font.pixelSize: 10
                }
            }
        }

        // buttons
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Item { Layout.fillWidth: true }
            Rectangle {
                width: 80; height: 32; radius: 6; color: root.cardColor
                Text { anchors.centerIn: parent; text: "Cancel"; color: root.textColor; font.pixelSize: 13 }
                MouseArea { anchors.fill: parent; onClicked: root.visible = false }
            }
            Rectangle {
                width: 80; height: 32; radius: 6; color: root.accentColor
                Text { anchors.centerIn: parent; text: "Apply"; color: "white"; font.pixelSize: 13; font.bold: true }
                MouseArea { anchors.fill: parent; onClicked: root.apply() }
            }
        }
    }
}
