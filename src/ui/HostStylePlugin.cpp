#include "ui/HostStylePlugin.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QPluginLoader>
#include <QStyle>
#include <QStylePlugin>

namespace speecher {

QStringList hostStylePluginDirs()
{
    return {QStringLiteral("/usr/lib64/qt6/plugins"),
            QStringLiteral("/usr/lib/x86_64-linux-gnu/qt6/plugins"),
            QStringLiteral("/usr/lib/aarch64-linux-gnu/qt6/plugins"),
            QStringLiteral("/usr/lib/qt6/plugins")};
}

QStyle *loadHostStyle(const QString &name, const QStringList &pluginDirs, QStringList *log)
{
    const auto note = [log](const QFileInfo &file, const QString &why) {
        if (log) {
            log->append(file.fileName() + QStringLiteral(": ") + why);
        }
    };
    for (const QString &dir : pluginDirs) {
        const QDir styles(dir + QStringLiteral("/styles"));
        const QFileInfoList files = styles.entryInfoList({QStringLiteral("*.so")}, QDir::Files);
        for (const QFileInfo &file : files) {
            QPluginLoader loader(file.absoluteFilePath());
            // The metadata is read without loading the library, so plugins for
            // other styles cost nothing and incompatible ones fail explained.
            const QJsonObject meta = loader.metaData();
            if (meta.isEmpty()) {
                note(file, loader.errorString());
                continue;
            }
            if (meta.value(QStringLiteral("IID")).toString()
                != QLatin1String("org.qt-project.Qt.QStyleFactoryInterface")) {
                continue;
            }
            const QJsonArray keys = meta.value(QStringLiteral("MetaData"))
                                        .toObject()
                                        .value(QStringLiteral("Keys"))
                                        .toArray();
            bool offersStyle = false;
            for (const QJsonValue &key : keys) {
                offersStyle = offersStyle || key.toString().compare(name, Qt::CaseInsensitive) == 0;
            }
            if (!offersStyle) {
                continue;
            }
            QObject *instance = loader.instance();
            if (!instance) {
                note(file, loader.errorString());
                continue;
            }
            auto *plugin = qobject_cast<QStylePlugin *>(instance);
            if (QStyle *style = plugin ? plugin->create(name) : nullptr) {
                // QStyleFactory names styles after their key; keep that so
                // the log and qApp->style()->objectName() read the same.
                style->setObjectName(name.toLower());
                return style;
            }
            note(file, QStringLiteral("did not create ") + name);
        }
    }
    return nullptr;
}

} // namespace speecher
