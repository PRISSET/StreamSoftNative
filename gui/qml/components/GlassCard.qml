import QtQuick
import QtQuick.Layouts
import StreamSoftGui

Item {
    id: card
    default property alias content: contentColumn.data
    property alias spacing: contentColumn.spacing
    implicitHeight: contentColumn.implicitHeight + 44

    GlassSurface {
        anchors.fill: parent
    }

    ColumnLayout {
        id: contentColumn
        anchors.fill: parent
        anchors.margins: 22
        spacing: 14
    }
}
