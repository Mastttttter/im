import QtQuick

QtObject {
    id: root
    property bool dark: true

    property color window: dark ? "#1e1e2e" : "#f5f5f5"
    property color panel: dark ? "#252535" : "#ffffff"
    property color panelAlt: dark ? "#2d2d3d" : "#f8f8f8"
    property color border: dark ? "#3d3d5c" : "#d0d0d0"
    property color text: dark ? "#e0e0e0" : "#2d2d2d"
    property color muted: dark ? "#a0a0b0" : "#606060"
    property color accent: dark ? "#4fc3f7" : "#0078d4"
    property color accentSoft: dark ? "#3d3d5c" : "#e3f2fd"
    property color danger: "#c62828"
    property color success: "#2e7d32"
    property color warning: "#ff9800"
    property color input: dark ? "#2d2d3d" : "#ffffff"
    property color chatBackground: dark ? "#1a1a2a" : "#ffffff"
    property color incomingBubble: dark ? "#2d2d3d" : "#f0f0f0"
    property color outgoingBubble: dark ? "#24445c" : "#d8ecff"
    property color systemBubble: dark ? "#252535" : "#f8f8f8"
    property string fontFamily: "Microsoft YaHei, Segoe UI, sans-serif"
}
