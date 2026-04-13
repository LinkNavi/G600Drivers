import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    spacing: 8

    property int profileIdx: 0
    property color textColor
    property color subColor
    property color cardColor
    property color accentColor

    property var onboard: config.getOnboard(profileIdx)

    onProfileIdxChanged: onboard = config.getOnboard(profileIdx)

    Connections {
        target: config
        function onProfilesChanged() { onboard = config.getOnboard(profileIdx) }
    }

    function save() {
        config.setOnboard(profileIdx, ringField.text, g7Field.text, g8Field.text)
        config.save()
    }

    Text { text: "ONBOARD BUTTONS"; color: subColor; font.pixelSize: 11; font.letterSpacing: 1.5 }
    Text {
        text: "Stored in mouse flash — work\neven without the daemon running."
        color: subColor; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true
    }

    // Ring
    ColumnLayout { Layout.fillWidth: true; spacing: 3
        Text { text: "Ring Finger"; color: subColor; font.pixelSize: 11 }
        Rectangle { Layout.fillWidth: true; height: 28; radius: 6; color: cardColor
            TextInput { id: ringField; anchors.fill: parent; anchors.margins: 6
                text: onboard.ring || "BTN_MIDDLE"
                color: textColor; font.pixelSize: 12; selectByMouse: true
                onEditingFinished: save() } }
    }

    // G7
    ColumnLayout { Layout.fillWidth: true; spacing: 3
        Text { text: "G7"; color: subColor; font.pixelSize: 11 }
        Rectangle { Layout.fillWidth: true; height: 28; radius: 6; color: cardColor
            TextInput { id: g7Field; anchors.fill: parent; anchors.margins: 6
                text: onboard.g7 || "BTN_MIDDLE"
                color: textColor; font.pixelSize: 12; selectByMouse: true
                onEditingFinished: save() } }
    }

    // G8
    ColumnLayout { Layout.fillWidth: true; spacing: 3
        Text { text: "G8"; color: subColor; font.pixelSize: 11 }
        Rectangle { Layout.fillWidth: true; height: 28; radius: 6; color: cardColor
            TextInput { id: g8Field; anchors.fill: parent; anchors.margins: 6
                text: onboard.g8 || "PROFILE_NEXT"
                color: textColor; font.pixelSize: 12; selectByMouse: true
                onEditingFinished: save() } }
    }

    Text {
        text: "Valid: BTN_MIDDLE, F1-F24,\nPROFILE_NEXT, DPI_UP, DPI_DOWN,\nany key name, NONE"
        color: subColor; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true
    }
}
