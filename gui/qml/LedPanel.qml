import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    spacing: 12

    property int profileIdx: 0
    property color textColor
    property color subColor
    property color cardColor
    property color accentColor

    property var led: config.getLed(profileIdx)

    onProfileIdxChanged: led = config.getLed(profileIdx)

    Connections {
        target: config
        function onProfilesChanged() { led = config.getLed(profileIdx) }
    }

    Timer {
        id: saveTimer
        interval: 400
        repeat: false
        onTriggered: config.save()
    }

    function setChannel(ch, val) {
        var r = ch === "r" ? val : (led.r || 0)
        var g = ch === "g" ? val : (led.g || 0)
        var b = ch === "b" ? val : (led.b || 0)
        config.setLed(profileIdx, r, g, b, led.effect || "solid", led.duration || 4)
        led = config.getLed(profileIdx)
        saveTimer.restart()
    }

    Text { text: "LED"; color: subColor; font.pixelSize: 11; font.letterSpacing: 1.5 }

    Rectangle {
        Layout.fillWidth: true
        height: 48; radius: 8
        color: led.enabled ? Qt.rgba((led.r||0)/255, (led.g||0)/255, (led.b||0)/255, 1) : "#333"
        Text {
            anchors.centerIn: parent
            text: led.enabled
                  ? "#%1%2%3".arg(("0"+(led.r||0).toString(16)).slice(-2))
                              .arg(("0"+(led.g||0).toString(16)).slice(-2))
                              .arg(("0"+(led.b||0).toString(16)).slice(-2)).toUpperCase()
                  : "No LED set"
            color: "white"; font.pixelSize: 13; font.bold: true
            style: Text.Outline; styleColor: "#00000060"
        }
    }

    // R slider
    RowLayout {
        Layout.fillWidth: true; spacing: 6
        Text { text: "R"; color: "#f38ba8"; font.pixelSize: 12; font.bold: true; Layout.preferredWidth: 12 }
        Slider {
            id: sliderR
            Layout.fillWidth: true
            from: 0; to: 255; stepSize: 1
            value: led.r || 0
            onMoved: setChannel("r", value)
        }
        Text { text: Math.round(led.r||0); color: textColor; font.pixelSize: 11; Layout.preferredWidth: 28 }
    }

    // G slider
    RowLayout {
        Layout.fillWidth: true; spacing: 6
        Text { text: "G"; color: "#a6e3a1"; font.pixelSize: 12; font.bold: true; Layout.preferredWidth: 12 }
        Slider {
            id: sliderG
            Layout.fillWidth: true
            from: 0; to: 255; stepSize: 1
            value: led.g || 0
            onMoved: setChannel("g", value)
        }
        Text { text: Math.round(led.g||0); color: textColor; font.pixelSize: 11; Layout.preferredWidth: 28 }
    }

    // B slider
    RowLayout {
        Layout.fillWidth: true; spacing: 6
        Text { text: "B"; color: "#89b4fa"; font.pixelSize: 12; font.bold: true; Layout.preferredWidth: 12 }
        Slider {
            id: sliderB
            Layout.fillWidth: true
            from: 0; to: 255; stepSize: 1
            value: led.b || 0
            onMoved: setChannel("b", value)
        }
        Text { text: Math.round(led.b||0); color: textColor; font.pixelSize: 11; Layout.preferredWidth: 28 }
    }

    Text { text: "Effect"; color: subColor; font.pixelSize: 11 }
    RowLayout {
        spacing: 6
        Repeater {
            model: ["solid", "breathe", "cycle"]
            delegate: Rectangle {
                Layout.fillWidth: true; height: 28; radius: 6
                color: (led.effect || "solid") === modelData ? accentColor : cardColor
                Text { anchors.centerIn: parent; text: modelData; color: textColor; font.pixelSize: 11 }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        config.setLed(profileIdx, led.r||0, led.g||0, led.b||0, modelData, led.duration||4)
                        led = config.getLed(profileIdx)
                    }
                }
            }
        }
    }

    ColumnLayout {
        visible: (led.effect || "solid") !== "solid"
        Layout.fillWidth: true; spacing: 4
        Text { text: "Duration (s)"; color: subColor; font.pixelSize: 11 }
        RowLayout {
            spacing: 6
            Slider {
                id: sliderDur
                Layout.fillWidth: true
                from: 1; to: 15; stepSize: 1
                value: led.duration || 4
                onMoved: {
                    config.setLed(profileIdx, led.r||0, led.g||0, led.b||0, led.effect||"solid", value)
                    led = config.getLed(profileIdx)
                    saveTimer.restart()
                }
            }
            Text { text: Math.round(led.duration||4) + "s"; color: textColor; font.pixelSize: 11; Layout.preferredWidth: 28 }
        }
    }

    Item { Layout.fillHeight: true }
}
