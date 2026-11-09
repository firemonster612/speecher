// The installed apps on macOS: the application bundles in the Applications
// folders and one level of folders inside them, such as Utilities.
#include "platform/InstalledApps.h"

#include <QByteArray>
#include <QDir>

#import <AppKit/AppKit.h>

#include <algorithm>

namespace speecher {

namespace {

void addBundle(NSString *path, QList<InstalledApp> &apps)
{
    NSBundle *bundle = [NSBundle bundleWithPath:path];
    NSString *identifier = bundle.bundleIdentifier;
    // Agents with no window of their own are never dictated into.
    if (!identifier || [[bundle objectForInfoDictionaryKey:@"LSBackgroundOnly"] boolValue]) {
        return;
    }
    NSString *name = [[NSFileManager defaultManager] displayNameAtPath:path];
    if ([name hasSuffix:@".app"]) {
        name = [name stringByDeletingPathExtension];
    }
    apps.append({QString::fromNSString(name), QString::fromNSString(path), {QString::fromNSString(identifier)},
                 QString::fromNSString(path)});
}

// The bundles in folder, and in the folders it holds that aren't bundles.
void addBundlesIn(NSString *folder, QList<InstalledApp> &apps, bool descend)
{
    NSFileManager *files = [NSFileManager defaultManager];
    for (NSString *entry in [files contentsOfDirectoryAtPath:folder error:nil]) {
        NSString *path = [folder stringByAppendingPathComponent:entry];
        BOOL directory = NO;
        if (![files fileExistsAtPath:path isDirectory:&directory] || !directory) {
            continue;
        }
        if ([entry.pathExtension isEqualToString:@"app"]) {
            addBundle(path, apps);
        } else if (descend) {
            addBundlesIn(path, apps, false);
        }
    }
}

} // namespace

QList<InstalledApp> installedApps()
{
    QList<InstalledApp> apps;
    @autoreleasepool {
        for (NSString *folder : {@"/Applications", @"/System/Applications",
                                 [NSHomeDirectory() stringByAppendingPathComponent:@"Applications"]}) {
            addBundlesIn(folder, apps, true);
        }
    }
    std::sort(apps.begin(), apps.end(), [](const InstalledApp &a, const InstalledApp &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    // One bundle id found twice, such as a copy in ~/Applications, lists once.
    apps.erase(std::unique(apps.begin(), apps.end(),
                           [](const InstalledApp &a, const InstalledApp &b) { return a.ids == b.ids; }),
               apps.end());
    return apps;
}

QImage installedAppIcon(const QString &key, int size)
{
    if (key.isEmpty()) {
        return {};
    }
    QImage image;
    @autoreleasepool {
        NSImage *icon = [[NSWorkspace sharedWorkspace] iconForFile:key.toNSString()];
        NSBitmapImageRep *bitmap = [NSBitmapImageRep imageRepWithData:icon.TIFFRepresentation];
        NSData *png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        if (png) {
            image = QImage::fromData(QByteArray::fromNSData(png));
        }
    }
    return image.isNull() ? image : image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

} // namespace speecher
