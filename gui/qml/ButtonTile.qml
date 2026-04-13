import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    width: 110; height: 64
    radius: 8
    color: hov ? Qt.lighter(surface, 1.2) : surface
    border.color: hov ? accentColor : "transparent"
    border.width: 1

    property string label: ""
    property string binding: ""
    property color accent
    property color surface
    property color textColor
    property color subColor
    property bool hov: false

    signal editRequested(string currentBinding)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 2

        Text {
            text: root.label
            color: root.subColor
            font.pixelSize: 10
            font.letterSpacing: 0.5
        }
        Text {
            Layout.fillWidth: true
            text: root.binding || "—"
            color: root.binding ? root.textColor : root.subColor
            font.pixelSize: 12
            elide: Text.ElideRight
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onEntered: root.hov = true
        onExited:  root.hov = false
        onClicked: root.editRequested(root.binding)
    }
}
