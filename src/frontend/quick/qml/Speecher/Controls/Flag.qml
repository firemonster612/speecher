import QtQuick
import Speecher.Theme

// A language's flag, round, from the circle-flags set bundled under
// assets/flags, so it looks the same on every platform (Windows has no
// colour flag emoji). Each language code has one flag, in files below; a
// code without one shows an empty circle.
Item {
    id: flag

    property string code
    property int size: 18

    // Language code to its flag, as circle-flags' language flags pair them;
    // Javanese (Whisper's jw), Sanskrit and Cantonese have none there.
    readonly property var files: ({
        af: "za", am: "et", ar: "language/ar", as: "in-as", az: "az", ba: "ru-ba", be: "by", bg: "bg",
        bn: "bd", bo: "cn-xz", br: "fr-bre", bs: "ba", ca: "es-ct", cs: "cz", cy: "gb-wls", da: "dk",
        de: "de", el: "gr", en: "gb", es: "es", et: "ee", eu: "es-pv", fa: "ir", fi: "fi", fo: "fo",
        fr: "fr", gl: "es-ga", gu: "in-gj", ha: "hausa", haw: "us-hi", he: "il", hi: "in", hr: "hr",
        ht: "ht", hu: "hu", hy: "am", id: "id", is: "is", it: "it", ja: "jp", jw: "id-jt", ka: "ge",
        kk: "kz", km: "kh", kn: "in-ka", ko: "kr", la: "language/la", lb: "lu", ln: "kongo", lo: "la",
        lt: "lt", lv: "lv", mg: "mg", mi: "maori", mk: "mk", ml: "malayali", mn: "mn", mr: "language/mr",
        ms: "my", mt: "mt", my: "mm", ne: "np", nl: "nl", nn: "no", no: "no", oc: "occitania", pa: "in",
        pl: "pl", ps: "af", pt: "pt", ro: "ro", ru: "ru", sa: "in", sd: "pk-sd", si: "lk", sk: "sk",
        sl: "si", sn: "zw", so: "so", sq: "al", sr: "rs", su: "id-jb", sv: "se", sw: "ke", ta: "in-tn",
        te: "in-tg", tg: "tj", th: "th", tk: "tm", tl: "ph", tr: "tr", tt: "ru-ta", uk: "ua", ur: "pk",
        uz: "uz", vi: "vn", yi: "language/yi", yo: "yorubaland", yue: "hk", zh: "cn"
    })

    width: size
    height: size
    Accessible.ignored: true

    Image {
        anchors.fill: parent
        source: flag.files[flag.code] ? "qrc:/speecher/assets/flags/" + flag.files[flag.code] + ".svg" : ""
        sourceSize: Qt.size(flag.size, flag.size)
        smooth: true
    }
    // A hairline keeps white and pale flags round on light paper.
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: "transparent"
        border.width: 1
        border.color: Theme.border
    }
}
