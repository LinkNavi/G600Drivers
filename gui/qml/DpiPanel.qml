import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    spacing: 10

    property int profileIdx: 0
    property color textColor
    property color subColor
    property color cardColor
    property color accentColor

    property var dpi: config.getDpi(profileIdx)

    onProfileIdxChanged: { dpi = config.getDpi(profileIdx) }

    Connections {
        target: config
        function onProfilesChanged() { dpi = config.getDpi(profileIdx) }
    }

    function save() {
        config.setDpi(profileIdx,
            parseInt(d1.text)||0, parseInt(d2.text)||0,
            parseInt(d3.text)||0, parseInt(d4.text)||0,
            defSlot.currentIndex + 1, parseInt(dshift.text)||0)
        config.save()
        dpi = config.getDpi(profileIdx)
    }

    Text { text: "DPI"; color: subColor; font.pixelSize: 11; font.letterSpacing: 1.5 }

    // 4 DPI slots
    Repeater {
        model: [
            {label: "Slot 1", key: "d1"},
            {label: "Slot 2", key: "d2"},
            {label: "Slot 3", key: "d3"},
            {label: "Slot 4", key: "d4"},
        ]
        delegate: RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text {
                text: modelData.label
                color: (dpi.def || 1) === (index + 1) ? accentColor : subColor
                font.pixelSize: 11
                Layout.preferredWidth: 46
            }
            Rectangle {
                Layout.fillWidth: true; height: 28; radius: 6; color: cardColor
                TextInput {
                    id: slotInput
                    anchors.fill: parent; anchors.margins: 6
                    text: dpi[modelData.key] || 0
                    color: textColor; font.pixelSize: 12; selectByMouse: true
                    validator: IntValidator { bottom: 0; top: 8200 }
                    onEditingFinished: save()
                }
            }
            Text { text: "dpi"; color: subColor; font.pixelSize: 10 }
        }
    }

    // default slot
    RowLayout {
        Layout.fillWidth: true; spacing: 6
        Text { text: "Default"; color: subColor; font.pixelSize: 11; Layout.preferredWidth: 46 }
        ComboBox {
            id: defSlot
            Layout.fillWidth: true
            model: ["1", "2", "3", "4"]
            currentIndex: (dpi.def || 1) - 1
            onActivated: save()
            contentItem: Text {
                leftPadding: 8
                text: defSlot.displayText
                color: textColor; font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle { color: cardColor; radius: 6 }
            popup.background: Rectangle { color: cardColor; radius: 6 }
        }
    }

    // G-Shift DPI
    RowLayout {
        Layout.fillWidth: true; spacing: 6
        Text { text: "G-Shift"; color: subColor; font.pixelSize: 11; Layout.preferredWidth: 46 }
        Rectangle {
            Layout.fillWidth: true; height: 28; radius: 6; color: cardColor
            TextInput {
                id: dshift
                anchors.fill: parent; anchors.margins: 6
                text: dpi.shift || 0
                color: textColor; font.pixelSize: 12; selectByMouse: true
                validator: IntValidator { bottom: 0; top: 8200 }
                onEditingFinished: save()
            }
        }
        Text { text: "dpi"; color: subColor; font.pixelSize: 10 }
    }
    Text { text: "0 = disabled. Steps of 50 (200-8200)"; color: subColor; font.pixelSize: 9 }
}
