import QtQuick
import QtQuick.Layouts
import StreamSoftGui

RowLayout {
    id: slot
    property string kindKey
    property string ext
    property bool exists: false
    signal requestUpload()
    signal requestDelete()

    spacing: 8

    Rectangle {
        width: 7
        height: 7
        radius: 3.5
        color: slot.exists ? Theme.good : "#55555c"
    }
    Text {
        text: (slot.ext === "gif" ? "Гифка" : "Звук") + (slot.exists ? " ✓" : "")
        color: Theme.textDim
        font.pixelSize: Theme.fontSm
    }
    PillButton {
        text: slot.exists ? "Заменить" : "Загрузить"
        implicitHeight: 30
        onClicked: slot.requestUpload()
    }
    PillButton {
        visible: slot.exists
        danger: true
        text: "✕"
        implicitHeight: 30
        implicitWidth: 36
        onClicked: slot.requestDelete()
    }
}
