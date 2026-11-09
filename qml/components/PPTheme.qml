import QtQuick

QtObject {
    readonly property color bgCanvas: "#0B0F14"
    readonly property color bgBar: "#10161E"
    readonly property color bgPanel: "#121923"
    readonly property color bgRaised: "#161F2B"
    readonly property color bgInput: "#0E151E"
    readonly property color bgSelected: "#102B38"
    readonly property color bgSuccess: "#102A22"
    readonly property color border: "#273342"
    readonly property color gridY: "#1B2734"
    readonly property color gridX: "#182432"
    readonly property color track: "#344256"
    readonly property color textPrimary: "#E7EDF4"
    readonly property color textMuted: "#7E8DA1"
    readonly property color cyan: "#21B6E9"
    readonly property color blue: "#4E8CFF"
    readonly property color purple: "#9D8CFF"
    readonly property color yellow: "#F5C451"
    readonly property color green: "#2AD879"
    readonly property color red: "#FF5B62"

    // Figma 规格字体是 Noto Sans SC，但它并非每台开发/交付机器都安装
    // （例如本机 Arch Linux 装的是 Noto Sans CJK SC / 思源黑体 CN）。
    // Qt 6 的 QFont::setFamily 不会拆分逗号列表，所以只能用 Qt.fontFamilies()
    // 显式挑第一个可用族。已安装 Noto Sans SC 的机器仍优先选中它，视觉规格不变。
    readonly property var fontFamilyCandidates: [
        "Noto Sans SC",
        "Noto Sans CJK SC",
        "Source Han Sans CN",
        "思源黑体 CN",
        "Microsoft YaHei",
        "PingFang SC"
    ]
    readonly property string fontFamily: {
        const available = Qt.fontFamilies()
        for (let index = 0; index < fontFamilyCandidates.length; ++index) {
            const candidate = fontFamilyCandidates[index]
            if (available.indexOf(candidate) !== -1)
                return candidate
        }
        return "sans-serif"
    }

    // Canvas 的 ctx.font 走的是 Qt 自己的 CSS 风格解析器，它把未加引号的族名
    // 按空格拆成多个候选族（见 Qt 6.8 qquickcontext2d.cpp 的
    // qExtractFontFamilies / qSetFontFamilyFromTokens），所以 "10px Noto Sans SC"
    // 会被拆成 Noto / Sans / SC 三个都不存在的族并报 invalid。含空格的族名必须
    // 加引号才能整体匹配；泛型族（sans-serif 等）去掉引号后仍走 style hint 分支。
    readonly property string canvasFontFamily: "'" + fontFamily + "'"
}
