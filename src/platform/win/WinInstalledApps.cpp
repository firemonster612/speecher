// The installed apps on Windows: the shell's Apps folder, which lists every
// Start menu app, Win32 and packaged alike.
#include "platform/InstalledApps.h"

#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>

#include <windows.h>
#include <appmodel.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <vector>

namespace speecher {

namespace {

using Microsoft::WRL::ComPtr;

// Defined here rather than linked from the SDK's import libraries.
constexpr GUID kAppsFolder{0x1e87508d, 0x89c2, 0x42f0, {0x8a, 0x7e, 0x64, 0x5a, 0x0f, 0x50, 0xca, 0x58}};
constexpr GUID kEnumItems{0x94f60519, 0x2850, 0x4924, {0xaa, 0x5a, 0xd1, 0x5e, 0x84, 0x86, 0x80, 0x39}};
constexpr PROPERTYKEY kAppUserModelId{{0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};
constexpr PROPERTYKEY kLinkTargetParsingPath{{0xB9B4B3FC, 0x2B51, 0x4A42, {0xB5, 0xD8, 0x32, 0x41, 0x46, 0xAF, 0xCF, 0x25}}, 2};

// COM for this thread, for as long as it lives: the scan runs on a worker
// and icons load on the image provider's thread.
class ComScope {
public:
    ComScope() : m_ok(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {}
    ~ComScope()
    {
        if (m_ok) {
            CoUninitialize();
        }
    }

private:
    bool m_ok;
};

QString takeString(PWSTR value)
{
    const QString text = value ? QString::fromWCharArray(value) : QString();
    CoTaskMemFree(value);
    return text;
}

QString displayName(IShellItem *item, SIGDN form)
{
    PWSTR name = nullptr;
    return SUCCEEDED(item->GetDisplayName(form, &name)) ? takeString(name) : QString();
}

QString property(IShellItem2 *item, const PROPERTYKEY &key)
{
    PWSTR value = nullptr;
    return SUCCEEDED(item->GetString(key, &value)) ? takeString(value) : QString();
}

QString programId(const QString &path)
{
    return QFileInfo(path).completeBaseName().toLower();
}

// Where the package a packaged app's AUMID ("family!app") names is installed,
// or empty.
QString packagePath(const QString &family)
{
    UINT32 count = 0;
    UINT32 length = 0;
    const std::wstring familyName = family.toStdWString();
    if (GetPackagesByPackageFamily(familyName.c_str(), &count, nullptr, &length, nullptr) != ERROR_INSUFFICIENT_BUFFER
        || count == 0) {
        return {};
    }
    std::vector<PWSTR> names(count);
    std::vector<wchar_t> buffer(length);
    if (GetPackagesByPackageFamily(familyName.c_str(), &count, names.data(), &length, buffer.data()) != ERROR_SUCCESS) {
        return {};
    }
    UINT32 pathLength = 0;
    if (GetPackagePathByFullName(names.front(), &pathLength, nullptr) != ERROR_INSUFFICIENT_BUFFER) {
        return {};
    }
    std::vector<wchar_t> path(pathLength);
    return GetPackagePathByFullName(names.front(), &pathLength, path.data()) == ERROR_SUCCESS
        ? QString::fromWCharArray(path.data())
        : QString();
}

// The program a packaged app runs, from its package's manifest, or empty.
QString packagedProgram(const QString &aumid)
{
    const QString location = packagePath(aumid.section(u'!', 0, 0));
    QFile manifest(location + QStringLiteral("\\AppxManifest.xml"));
    if (location.isEmpty() || !manifest.open(QIODevice::ReadOnly)) {
        return {};
    }
    QXmlStreamReader xml(&manifest);
    while (!xml.atEnd()) {
        if (xml.readNext() == QXmlStreamReader::StartElement && xml.name() == u"Application"
            && xml.attributes().value(u"Id") == aumid.section(u'!', 1)) {
            return programId(xml.attributes().value(u"Executable").toString());
        }
    }
    return {};
}

} // namespace

QList<InstalledApp> installedApps()
{
    const ComScope com;
    QList<InstalledApp> apps;
    ComPtr<IShellItem> folder;
    ComPtr<IEnumShellItems> items;
    if (FAILED(SHGetKnownFolderItem(kAppsFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&folder)))
        || FAILED(folder->BindToHandler(nullptr, kEnumItems, IID_PPV_ARGS(&items)))) {
        return apps;
    }
    ComPtr<IShellItem> item;
    while (items->Next(1, &item, nullptr) == S_OK) {
        ComPtr<IShellItem2> details;
        if (FAILED(item.As(&details))) {
            continue;
        }
        const QString target = property(details.Get(), kLinkTargetParsingPath);
        const QString aumid = property(details.Get(), kAppUserModelId);
        QStringList ids;
        // A Win32 app is known by its program; a packaged one by its AUMID,
        // and by the program its manifest names where that can be read.
        if (target.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
            ids.append(programId(target));
        } else if (aumid.contains(u'!')) {
            ids.append(aumid);
            const QString program = packagedProgram(aumid);
            if (!program.isEmpty()) {
                ids.append(program);
            }
        }
        const QString name = displayName(item.Get(), SIGDN_NORMALDISPLAY);
        if (!ids.isEmpty() && !name.isEmpty()) {
            const QString parsingName = displayName(item.Get(), SIGDN_DESKTOPABSOLUTEPARSING);
            apps.append({name, parsingName, ids, parsingName});
        }
        item.Reset();
    }
    std::sort(apps.begin(), apps.end(), [](const InstalledApp &a, const InstalledApp &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return apps;
}

QImage installedAppIcon(const QString &key, int size)
{
    if (key.isEmpty()) {
        return {};
    }
    const ComScope com;
    ComPtr<IShellItemImageFactory> factory;
    HBITMAP bitmap = nullptr;
    if (FAILED(SHCreateItemFromParsingName(key.toStdWString().c_str(), nullptr, IID_PPV_ARGS(&factory)))
        || FAILED(factory->GetImage({size, size}, SIIGBF_ICONONLY, &bitmap))) {
        return {};
    }
    const QImage image = QImage::fromHBITMAP(bitmap);
    DeleteObject(bitmap);
    return image;
}

} // namespace speecher
