import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    visible: true
    width: 1000
    height: 680
    title: "G600 Config"
    color: "#1e1e2e"

    readonly property color bg:      "#1e1e2e"
    readonly property color surface: "#2a2a3e"
    readonly property color card:    "#313145"
    readonly property color accent:  "#7c5cfc"
    readonly property color text:    "#cdd6f4"
    readonly property color subtext: "#7f849c"
    readonly property color border:  "#45475a"

    property int  selProfile: 0
    property bool gshiftLayer: false

    readonly property var gkeys: ["G9","G10","G11","G12","G13","G14","G15","G16","G17","G18","G19","G20"]
    readonly property var mkeys: ["BTN_LEFT","BTN_RIGHT","MIDDLE","TILT_LEFT","TILT_RIGHT"]
    readonly property var mkeyLabels: ["Left Click","Right Click","Middle Click","Tilt Left","Tilt Right"]

    function buttonKey(name) {
        return gshiftLayer ? "SHIFT_" + name : name
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ── LEFT: profile panel ───────────────────
        Rectangle {
            Layout.preferredWidth: 200
            Layout.fillHeight: true
            color: surface

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                Text {
                    text: "PROFILES"
                    color: subtext
                    font.pixelSize: 11
                    font.letterSpacing: 1.5
                }

                ListView {
                    id: profileList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: config.profiles

                    delegate: Rectangle {
                        id: profileDelegate
                        width: profileList.width
                        height: 40
                        radius: 6
                        color: selProfile === index ? accent : (profileMa.containsMouse ? card : "transparent")

                        // click selects, only editing via rename button
                        MouseArea {
                            id: profileMa
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                selProfile = index
                                config.setCurrentProfile(index)
                            }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 6

                            Text {
                                id: profileNameText
                                Layout.fillWidth: true
                                text: modelData.name
                                color: root.text
                                font.pixelSize: 13
                                visible: !nameEdit.visible
                            }

                            TextInput {
                                id: nameEdit
                                Layout.fillWidth: true
                                text: modelData.name
                                color: root.text
                                font.pixelSize: 13
                                visible: false
                                selectByMouse: true
                                onEditingFinished: {
                                    config.setProfileName(index, text)
                                    visible = false
                                }
                                Keys.onEscapePressed: visible = false
                            }

                            // rename button
                            Rectangle {
                                width: 20; height: 20; radius: 4
                                color: renHov.containsMouse ? root.accent : "transparent"
                                Text { anchors.centerIn: parent; text: "✎"; color: root.text; font.pixelSize: 11 }
                                MouseArea {
                                    id: renHov
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        nameEdit.text = modelData.name
                                        nameEdit.visible = true
                                        nameEdit.forceActiveFocus()
                                        nameEdit.selectAll()
                                    }
                                }
                            }

                            Rectangle {
                                width: 20; height: 20; radius: 4
                                color: delHov.containsMouse ? "#f38ba8" : "transparent"
                                visible: config.profiles.length > 1
                                Text { anchors.centerIn: parent; text: "✕"; color: root.text; font.pixelSize: 11 }
                                MouseArea {
                                    id: delHov
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        config.removeProfile(index)
                                        if (selProfile >= config.profiles.length)
                                            selProfile = config.profiles.length - 1
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 36; radius: 6
                    color: addMa.containsMouse ? accent : card
                    Text { anchors.centerIn: parent; text: "+ Add Profile"; color: root.text; font.pixelSize: 13 }
                    MouseArea { id: addMa; anchors.fill: parent; hoverEnabled: true
                        onClicked: config.addProfile("profile " + config.profiles.length) }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text { text: "Ring Button Profile ID"; color: subtext; font.pixelSize: 11 }
                    Rectangle {
                        Layout.fillWidth: true; height: 32; radius: 6; color: card
                        TextInput {
                            anchors.fill: parent; anchors.margins: 8
                            text: config.profiles.length > selProfile ? config.profiles[selProfile].abs_misc : "-1"
                            color: root.text; font.pixelSize: 13; selectByMouse: true
                            validator: IntValidator { bottom: -1; top: 255 }
                            onEditingFinished: config.setAbsMisc(selProfile, parseInt(text))
                        }
                    }
                }
            }
        }

        // ── CENTRE ────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                height: 48; color: card

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    Rectangle {
                        width: 130; height: 32; radius: 16
                        color: gshiftLayer ? accent : border
                        Text { anchors.centerIn: parent; text: gshiftLayer ? "G-Shift Layer" : "Normal Layer"
                               color: root.text; font.pixelSize: 12 }
                        MouseArea { anchors.fill: parent; onClicked: gshiftLayer = !gshiftLayer }
                    }

                    Item { Layout.fillWidth: true }

                    Rectangle {
                        width: 80; height: 32; radius: 6
                        color: saveMa.containsMouse ? Qt.lighter(accent) : accent
                        Text { anchors.centerIn: parent; text: "Save"; color: "white"; font.pixelSize: 13; font.bold: true }
                        MouseArea { id: saveMa; anchors.fill: parent; hoverEnabled: true; onClicked: config.save() }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: bg

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 16

                    Text { text: "SIDE BUTTONS"; color: subtext; font.pixelSize: 11; font.letterSpacing: 1.5 }

                    GridLayout {
                        columns: 4
                        columnSpacing: 10
                        rowSpacing: 10

                        Repeater {
                            model: gkeys
                            delegate: ButtonTile {
                                label: modelData
                                binding: config.profiles.length > selProfile
                                         ? config.getBinding(selProfile, buttonKey(modelData)) : ""
                                accent: root.accent; surface: root.card
                                textColor: root.text; subColor: root.subtext
                                onEditRequested: bindingEditor.open(selProfile, buttonKey(modelData), binding)
                            }
                        }
                    }

                    Text { text: "MOUSE BUTTONS"; color: subtext; font.pixelSize: 11; font.letterSpacing: 1.5 }

                    RowLayout {
                        spacing: 10
                        Repeater {
                            model: mkeys
                            delegate: ButtonTile {
                                label: mkeyLabels[index]
                                binding: config.profiles.length > selProfile
                                         ? config.getBinding(selProfile, modelData) : ""
                                accent: root.accent; surface: root.card
                                textColor: root.text; subColor: root.subtext
                                onEditRequested: bindingEditor.open(selProfile, modelData, binding)
                            }
                        }
                    }
                }
            }
        }

        // ── RIGHT: LED ────────────────────────────
        Rectangle {
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            color: surface

            ScrollView {
                anchors.fill: parent
                contentWidth: -1
                clip: true

                ColumnLayout {
                    width: 196
                    spacing: 16
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: 12

                    LedPanel {
                        Layout.fillWidth: true
                        profileIdx: selProfile
                        textColor: root.text; subColor: root.subtext
                        cardColor: root.card; accentColor: root.accent
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: root.border }

                    DpiPanel {
                        Layout.fillWidth: true
                        profileIdx: selProfile
                        textColor: root.text; subColor: root.subtext
                        cardColor: root.card; accentColor: root.accent
                    }

                    Item { Layout.preferredHeight: 12 }
                }
            }
        }
    }

    BindingEditor {
        id: bindingEditor
        textColor: root.text; subColor: root.subtext
        cardColor: root.card; accentColor: root.accent
        bgColor: root.surface
    }
}
