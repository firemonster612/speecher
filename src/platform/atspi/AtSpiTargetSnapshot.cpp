#include "platform/atspi/AtSpiTargetSnapshot.h"

#include <QCryptographicHash>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#ifdef SPEECHER_WITH_ATSPI
#include <unistd.h>
#endif

namespace speecher::atspi {

#ifdef SPEECHER_WITH_ATSPI
namespace {

constexpr int contextCharacters = 240;
constexpr int maximumVisitedObjects = 4000;
constexpr int maximumTreeDepth = 40;
constexpr int maximumDescendantProcesses = 64;
constexpr int maximumAncestorProcesses = 16;

qint64 processIdForBusName(const QString &busName, QHash<QString, qint64> *cache)
{
    const auto cached = cache->constFind(busName);
    if (cached != cache->cend()) {
        return *cached;
    }

    qint64 processId = -1;
    DBusConnection *bus = atspi_get_a11y_bus();
    DBusMessage *message = dbus_message_new_method_call(
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "GetConnectionUnixProcessID");
    const QByteArray utf8 = busName.toUtf8();
    const char *name = utf8.constData();
    if (bus && message
        && dbus_message_append_args(message,
                                    DBUS_TYPE_STRING,
                                    &name,
                                    DBUS_TYPE_INVALID)) {
        DBusError error = DBUS_ERROR_INIT;
        DBusMessage *reply = dbus_connection_send_with_reply_and_block(
            bus, message, 2000, &error);
        if (reply) {
            dbus_uint32_t value = 0;
            if (dbus_message_get_args(reply,
                                      nullptr,
                                      DBUS_TYPE_UINT32,
                                      &value,
                                      DBUS_TYPE_INVALID)) {
                processId = value;
            }
            dbus_message_unref(reply);
        }
        if (dbus_error_is_set(&error)) {
            dbus_error_free(&error);
        }
    }
    if (message) {
        dbus_message_unref(message);
    }
    cache->insert(busName, processId);
    return processId;
}

bool isSelfAccessible(AtspiAccessible *accessible, QHash<QString, qint64> *processIds)
{
    if (!accessible) {
        return false;
    }
    AtspiObject *object = ATSPI_OBJECT(accessible);
    const char *busName = object && object->app ? object->app->bus_name : nullptr;
    return busName && *busName
        && processIdForBusName(QString::fromUtf8(busName), processIds) == getpid();
}

bool isForeignPrivilegedProcess(qint64 processId)
{
    if (processId <= 0) {
        return false;
    }
    QFile status(QStringLiteral("/proc/%1/status").arg(processId));
    if (!status.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    const QRegularExpressionMatch match = QRegularExpression(
        QStringLiteral("(?m)^Uid:\\s+(\\d+)")).match(QString::fromUtf8(status.readAll()));
    return match.hasMatch() && match.captured(1).toUInt() != uint(getuid());
}

AtspiAccessible *focusedObject(AtspiAccessible *object,
                               int depth,
                               int *visited,
                               QHash<QString, qint64> *processIds)
{
    if (!object || depth > maximumTreeDepth || ++(*visited) > maximumVisitedObjects
        || isSelfAccessible(object, processIds)) {
        return nullptr;
    }
    GError *error = nullptr;
    const int childCount = atspi_accessible_get_child_count(object, &error);
    clearError(&error);
    for (int index = 0; index < childCount; ++index) {
        AtspiAccessible *child = atspi_accessible_get_child_at_index(object, index, &error);
        clearError(&error);
        if (!child) {
            continue;
        }
        AtspiAccessible *focused = focusedObject(child, depth + 1, visited, processIds);
        g_object_unref(child);
        if (focused) {
            return focused;
        }
    }
    return hasState(object, ATSPI_STATE_FOCUSED)
        ? ATSPI_ACCESSIBLE(g_object_ref(object))
        : nullptr;
}

AtspiAccessible *focusedObjectInActiveWindow(AtspiAccessible *desktop,
                                             QHash<QString, qint64> *processIds)
{
    if (!desktop) {
        return nullptr;
    }
    AtspiAccessible *fallbackWindow = nullptr;
    int fallbackScore = -1;
    GError *error = nullptr;
    const int applicationCount = atspi_accessible_get_child_count(desktop, &error);
    clearError(&error);
    for (int applicationIndex = 0; applicationIndex < applicationCount; ++applicationIndex) {
        AtspiAccessible *application = atspi_accessible_get_child_at_index(desktop, applicationIndex, &error);
        clearError(&error);
        if (!application || isSelfAccessible(application, processIds)) {
            if (application) g_object_unref(application);
            continue;
        }
        if (hasState(application, ATSPI_STATE_ACTIVE)) {
            int visited = 0;
            AtspiAccessible *focused = focusedObject(application, 0, &visited, processIds);
            if (focused) {
                if (fallbackWindow) g_object_unref(fallbackWindow);
                g_object_unref(application);
                return focused;
            }
        }
        const int windowCount = atspi_accessible_get_child_count(application, &error);
        clearError(&error);
        for (int windowIndex = 0; windowIndex < windowCount; ++windowIndex) {
            AtspiAccessible *window = atspi_accessible_get_child_at_index(application, windowIndex, &error);
            clearError(&error);
            if (!window || isSelfAccessible(window, processIds)) {
                if (window) g_object_unref(window);
                continue;
            }
            if (hasState(window, ATSPI_STATE_ACTIVE)) {
                int visited = 0;
                AtspiAccessible *focused = focusedObject(window, 0, &visited, processIds);
                if (focused) {
                    if (fallbackWindow) g_object_unref(fallbackWindow);
                    g_object_unref(window);
                    g_object_unref(application);
                    return focused;
                }
                const QString name = takeString(atspi_accessible_get_name(window, &error)).trimmed();
                clearError(&error);
                const int score = (name.isEmpty() ? 0 : 100)
                    + (hasState(window, ATSPI_STATE_SHOWING) ? 20 : 0)
                    + (hasState(window, ATSPI_STATE_VISIBLE) ? 10 : 0);
                if (score > fallbackScore) {
                    if (fallbackWindow) g_object_unref(fallbackWindow);
                    fallbackWindow = ATSPI_ACCESSIBLE(g_object_ref(window));
                    fallbackScore = score;
                }
            }
            g_object_unref(window);
        }
        g_object_unref(application);
    }
    return fallbackWindow;
}

void findBestEditableText(AtspiAccessible *object, int depth, int *visited,
                          AtspiAccessible **best, int *bestScore,
                          QHash<QString, qint64> *processIds)
{
    if (!object || depth > maximumTreeDepth || ++(*visited) > maximumVisitedObjects
        || isSelfAccessible(object, processIds)) {
        return;
    }
    if (atspi_accessible_is_editable_text(object) && atspi_accessible_is_text(object)) {
        AtspiText *text = atspi_accessible_get_text(object);
        GError *error = nullptr;
        const int caret = text ? atspi_text_get_caret_offset(text, &error) : -1;
        clearError(&error);
        const int count = text ? atspi_text_get_character_count(text, &error) : -1;
        clearError(&error);
        if (caret >= 0 && count >= 0) {
            int score = 1000 + qMin(count, 500);
            if (caret > 0) score += 200;
            if (hasState(object, ATSPI_STATE_MULTI_LINE)) score += 100;
            if (hasState(object, ATSPI_STATE_FOCUSED)) score += 2000;
            if (score > *bestScore) {
                if (*best) g_object_unref(*best);
                *best = ATSPI_ACCESSIBLE(g_object_ref(object));
                *bestScore = score;
            }
        }
    }
    GError *error = nullptr;
    const int childCount = atspi_accessible_get_child_count(object, &error);
    clearError(&error);
    for (int index = 0; index < childCount; ++index) {
        AtspiAccessible *child = atspi_accessible_get_child_at_index(object, index, &error);
        clearError(&error);
        if (child) {
            findBestEditableText(child, depth + 1, visited, best, bestScore, processIds);
            g_object_unref(child);
        }
    }
}

AtspiAccessible *activeWindowAncestor(AtspiAccessible *object,
                                      QHash<QString, qint64> *processIds)
{
    AtspiAccessible *current = object ? ATSPI_ACCESSIBLE(g_object_ref(object)) : nullptr;
    for (int depth = 0; current && depth < maximumTreeDepth; ++depth) {
        if (isSelfAccessible(current, processIds)) break;
        if (hasState(current, ATSPI_STATE_ACTIVE)) return current;
        GError *error = nullptr;
        AtspiAccessible *parent = atspi_accessible_get_parent(current, &error);
        clearError(&error);
        g_object_unref(current);
        current = parent;
    }
    if (current) g_object_unref(current);
    return nullptr;
}

AtspiAccessible *kTextEditorFallback(AtspiAccessible *focused,
                                    const QString &applicationName,
                                    const QString &process,
                                    QHash<QString, qint64> *processIds)
{
    const QString identity = applicationName + QLatin1Char(' ') + process;
    if (!focused || atspi_accessible_is_editable_text(focused)
        || (!identity.contains(QStringLiteral("kate"), Qt::CaseInsensitive)
            && !identity.contains(QStringLiteral("kwrite"), Qt::CaseInsensitive))) {
        return nullptr;
    }
    AtspiAccessible *window = activeWindowAncestor(focused, processIds);
    if (!window) return nullptr;
    int visited = 0;
    int bestScore = -1;
    AtspiAccessible *best = nullptr;
    findBestEditableText(window, 0, &visited, &best, &bestScore, processIds);
    g_object_unref(window);
    return best;
}

QString applicationAttribute(AtspiAccessible *application)
{
    GError *error = nullptr;
    GHashTable *attributes = atspi_accessible_get_attributes(application, &error);
    clearError(&error);
    if (!attributes) return {};
    QString result;
    for (const char *key : {"desktop-entry", "application-id", "id"}) {
        if (const auto *value = static_cast<const char *>(g_hash_table_lookup(attributes, key))) {
            result = QString::fromUtf8(value).trimmed();
            if (!result.isEmpty()) break;
        }
    }
    g_hash_table_unref(attributes);
    return result;
}

QString accessibleAttribute(AtspiAccessible *object, const QList<QByteArray> &keys)
{
    GError *error = nullptr;
    GHashTable *attributes = atspi_accessible_get_attributes(object, &error);
    clearError(&error);
    if (!attributes) return {};
    QString result;
    for (const QByteArray &key : keys) {
        if (const auto *value = static_cast<const char *>(g_hash_table_lookup(attributes, key.constData()))) {
            result = QString::fromUtf8(value).trimmed();
            if (!result.isEmpty()) break;
        }
    }
    g_hash_table_unref(attributes);
    return result;
}

void populateAncestorContext(Target *target,
                             AtspiAccessible *focused,
                             QHash<QString, qint64> *processIds)
{
    AtspiAccessible *current = focused ? ATSPI_ACCESSIBLE(g_object_ref(focused)) : nullptr;
    for (int depth = 0; current && depth < maximumTreeDepth; ++depth) {
        if (isSelfAccessible(current, processIds)) break;
        GError *error = nullptr;
        const QString role = takeString(atspi_accessible_get_role_name(current, &error)).toLower();
        clearError(&error);
        const QString name = takeString(atspi_accessible_get_name(current, &error)).trimmed();
        clearError(&error);
        if (target->windowTitle.isEmpty() && !name.isEmpty()
            && (role.contains(QStringLiteral("frame"))
                || role.contains(QStringLiteral("window"))
                || role.contains(QStringLiteral("dialog")))) {
            target->windowTitle = name;
        }
        if (target->documentUrl.isEmpty()) {
            target->documentUrl = accessibleAttribute(current,
                {QByteArrayLiteral("DocURL"), QByteArrayLiteral("doc-url"),
                 QByteArrayLiteral("document-url"), QByteArrayLiteral("url"),
                 QByteArrayLiteral("uri")});
        }
        AtspiAccessible *parent = atspi_accessible_get_parent(current, &error);
        clearError(&error);
        g_object_unref(current);
        current = parent;
    }
}

QString processName(qint64 processId)
{
    QFile file(QStringLiteral("/proc/%1/comm").arg(processId));
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

qint64 parentProcessId(qint64 processId)
{
    QFile file(QStringLiteral("/proc/%1/status").arg(processId));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;
    const QRegularExpressionMatch match = QRegularExpression(
        QStringLiteral("(?m)^PPid:\\s+(\\d+)")).match(QString::fromUtf8(file.readAll()));
    return match.hasMatch() ? match.captured(1).toLongLong() : 0;
}

QString executableName(const QString &path)
{
    return path.sliced(path.lastIndexOf(QLatin1Char('/')) + 1).trimmed().toLower();
}

bool processLooksLikeAiCodingTool(qint64 processId)
{
    static const QSet<QString> commands{
        QStringLiteral("aider"),
        QStringLiteral("amp"),
        QStringLiteral("auggie"),
        QStringLiteral("claude"),
        QStringLiteral("cline"),
        QStringLiteral("codex"),
        QStringLiteral("copilot"),
        QStringLiteral("crush"),
        QStringLiteral("cursor-agent"),
        QStringLiteral("droid"),
        QStringLiteral("gemini"),
        QStringLiteral("goose"),
        QStringLiteral("kiro-cli"),
        QStringLiteral("kilocode"),
        QStringLiteral("kimi"),
        QStringLiteral("opencode"),
        QStringLiteral("openhands"),
        QStringLiteral("pi"),
        QStringLiteral("q"),
        QStringLiteral("qwen"),
        QStringLiteral("replit"),
        QStringLiteral("roo"),
        QStringLiteral("trae"),
        QStringLiteral("vibe"),
        QStringLiteral("windsurf"),
    };
    if (commands.contains(processName(processId).toLower())) {
        return true;
    }

    QFile file(QStringLiteral("/proc/%1/cmdline").arg(processId));
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QList<QByteArray> arguments = file.read(16 * 1024).split('\0');
    QStringList executableParts;
    for (int index = 0; index < arguments.size() && index < 2; ++index) {
        const QString argument = QString::fromLocal8Bit(arguments.at(index)).trimmed();
        if (!argument.isEmpty()) {
            executableParts.append(argument);
            executableParts.append(executableName(argument));
        }
    }
    if (executableParts.isEmpty()) return false;
    if (commands.contains(executableName(executableParts.first()))) {
        return true;
    }

    Target probe;
    probe.processName = executableParts.join(QLatin1Char(' '));
    return classifyTarget(probe) == AppCategory::AiCoding;
}

bool processHasTerminalAncestor(qint64 processId)
{
    static const QSet<QString> shells{
        QStringLiteral("bash"),
        QStringLiteral("dash"),
        QStringLiteral("fish"),
        QStringLiteral("nu"),
        QStringLiteral("screen"),
        QStringLiteral("sh"),
        QStringLiteral("tmux"),
        QStringLiteral("zsh"),
    };
    const QString currentProcess = processName(processId).toLower();
    if (!shells.contains(currentProcess) && !processLooksLikeAiCodingTool(processId)) {
        return false;
    }

    QSet<qint64> visited;
    for (int depth = 0; processId > 1 && depth < maximumAncestorProcesses; ++depth) {
        processId = parentProcessId(processId);
        if (processId <= 1 || visited.contains(processId)) break;
        visited.insert(processId);

        Target ancestor;
        ancestor.processName = processName(processId);
        QFile cmdline(QStringLiteral("/proc/%1/cmdline").arg(processId));
        if (cmdline.open(QIODevice::ReadOnly)) {
            ancestor.applicationName = QString::fromLocal8Bit(cmdline.read(4096)).replace(QChar::Null, QLatin1Char(' '));
        }
        if (isTerminalTarget(ancestor)) return true;
    }
    return false;
}

bool terminalHasAiCodingTool(const Target &target)
{
    Target titleProbe;
    titleProbe.applicationName = target.windowTitle;
    if (classifyTarget(titleProbe) == AppCategory::AiCoding) {
        return true;
    }

    QList<qint64> pending{target.processId};
    QSet<qint64> visited;
    while (!pending.isEmpty() && visited.size() < maximumDescendantProcesses) {
        const qint64 processId = pending.takeFirst();
        if (processId <= 0 || visited.contains(processId)) continue;
        visited.insert(processId);
        if (processLooksLikeAiCodingTool(processId)) return true;

        QFile children(QStringLiteral("/proc/%1/task/%1/children").arg(processId));
        if (!children.open(QIODevice::ReadOnly)) continue;
        const QStringList childIds = QString::fromLatin1(children.readAll()).split(
            QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        for (const QString &childId : childIds) {
            bool valid = false;
            const qint64 childProcessId = childId.toLongLong(&valid);
            if (valid && !visited.contains(childProcessId)) pending.append(childProcessId);
        }
    }
    return false;
}

void populateText(Target *target, AtspiAccessible *object)
{
    if (!atspi_accessible_is_text(object)) return;
    AtspiText *text = atspi_accessible_get_text(object);
    if (!text) return;
    GError *error = nullptr;
    const int count = atspi_text_get_character_count(text, &error);
    clearError(&error);
    const int caret = atspi_text_get_caret_offset(text, &error);
    clearError(&error);
    if (count < 0 || caret < 0) return;
    target->caretOffset = caret;
    const int start = qMax(0, caret - contextCharacters);
    const int end = qMin(count, caret + contextCharacters);
    target->nearbyTextBefore = takeString(atspi_text_get_text(text, start, caret, &error));
    clearError(&error);
    target->nearbyTextAfter = takeString(atspi_text_get_text(text, caret, end, &error));
    clearError(&error);
    if (atspi_text_get_n_selections(text, &error) > 0) {
        clearError(&error);
        AtspiRange *selection = atspi_text_get_selection(text, 0, &error);
        clearError(&error);
        if (selection) {
            target->selectionStart = selection->start_offset;
            target->selectionEnd = selection->end_offset;
            if (selection->end_offset > selection->start_offset) {
                target->selectedText = takeString(atspi_text_get_text(
                    text, selection->start_offset,
                    selection->end_offset,
                    &error));
                clearError(&error);
            }
            g_free(selection);
        }
    } else {
        clearError(&error);
    }
}

QString fingerprint(const Target &target)
{
    const QByteArray material = target.applicationId.toUtf8()
        + '\0' + QByteArray::number(target.processId)
        + '\0' + target.windowTitle.toUtf8()
        + '\0' + target.documentUrl.toUtf8()
        + '\0' + QByteArray::number(target.caretOffset)
        + '\0' + QByteArray::number(target.selectionStart)
        + '\0' + QByteArray::number(target.selectionEnd)
        + '\0' + target.nearbyTextBefore.toUtf8()
        + '\0' + target.nearbyTextAfter.toUtf8()
        + '\0' + target.selectedText.toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(material, QCryptographicHash::Sha256).toHex());
}

} // namespace
#endif

TargetSnapshot TargetSnapshot::capture()
{
    TargetSnapshot snapshot;
#ifdef SPEECHER_WITH_ATSPI
    if (!atspi_is_initialized() && atspi_init() != 0) return snapshot;
    QHash<QString, qint64> processIds;
    for (int desktopIndex = 0; desktopIndex < atspi_get_desktop_count(); ++desktopIndex) {
        AccessibleHandle desktop(atspi_get_desktop(desktopIndex));
        AtspiAccessible *focused = focusedObjectInActiveWindow(desktop.get(), &processIds);
        if (!focused) {
            int visited = 0;
            focused = focusedObject(desktop.get(), 0, &visited, &processIds);
        }
        if (!focused) continue;
        GError *error = nullptr;
        AccessibleHandle application(atspi_accessible_get_application(focused, &error));
        clearError(&error);
        if (!application || isSelfAccessible(application.get(), &processIds)) {
            g_object_unref(focused);
            continue;
        }
        Target &target = snapshot.m_target;
        target.applicationName = application ? takeString(atspi_accessible_get_name(application.get(), &error)) : QString();
        clearError(&error);
        target.applicationId = application ? applicationAttribute(application.get()) : QString();
        target.processId = application ? atspi_accessible_get_process_id(application.get(), &error) : 0;
        clearError(&error);
        target.processName = processName(target.processId);
        if (AtspiAccessible *editor = kTextEditorFallback(
                focused, target.applicationName, target.processName, &processIds)) {
            g_object_unref(focused);
            focused = editor;
        }
        if (target.applicationId.isEmpty()) target.applicationId = target.processName.toLower();
        target.controlName = takeString(atspi_accessible_get_name(focused, &error));
        clearError(&error);
        target.role = takeString(atspi_accessible_get_role_name(focused, &error));
        clearError(&error);
        target.toolkit = application ? takeString(atspi_accessible_get_toolkit_name(application.get(), &error)) : QString();
        clearError(&error);
        target.secure = atspi_accessible_get_role(focused, &error) == ATSPI_ROLE_PASSWORD_TEXT;
        clearError(&error);
        target.secure = target.secure || isForeignPrivilegedProcess(target.processId);
        target.accessible = true;
        if (!target.secure) {
            populateAncestorContext(&target, focused, &processIds);
            populateText(&target, focused);
        }
        target.terminalHost = isTerminalTarget(target)
            || processHasTerminalAncestor(target.processId);
        if (target.terminalHost) {
            target.aiCodingToolActive = terminalHasAiCodingTool(target);
        }
        target.category = classifyTarget(target);
        target.fingerprint = fingerprint(target);
        snapshot.m_accessible = AccessibleHandle(focused);
        break;
    }
#endif
    return snapshot;
}

const Target &TargetSnapshot::target() const { return m_target; }

bool TargetSnapshot::valid() const
{
    return bool(m_accessible);
}

bool TargetSnapshot::matches(const Target &target, bool requireFocus) const
{
#ifdef SPEECHER_WITH_ATSPI
    if (!m_accessible || target.secure || target.fingerprint != m_target.fingerprint) return false;
    AtspiStateSet *states = atspi_accessible_get_state_set(m_accessible.get());
    const bool focused = states && atspi_state_set_contains(states, ATSPI_STATE_FOCUSED);
    const bool defunct = !states || atspi_state_set_contains(states, ATSPI_STATE_DEFUNCT);
    if (states) g_object_unref(states);
    if (defunct || (requireFocus && !focused)) return false;
    Target current = m_target;
    current.nearbyTextBefore.clear();
    current.nearbyTextAfter.clear();
    current.selectedText.clear();
    current.caretOffset = current.selectionStart = current.selectionEnd = -1;
    populateText(&current, m_accessible.get());
    current.fingerprint = fingerprint(current);
    return current.fingerprint == target.fingerprint;
#else
    Q_UNUSED(target)
    Q_UNUSED(requireFocus)
    return false;
#endif
}

bool TargetSnapshot::canInsert(const Target &target) const
{
#ifdef SPEECHER_WITH_ATSPI
    return m_accessible && !target.secure && target.caretOffset >= 0
        && !(target.selectionStart >= 0 && target.selectionEnd > target.selectionStart)
        && atspi_accessible_is_editable_text(m_accessible.get()) && matches(target, false);
#else
    Q_UNUSED(target)
    return false;
#endif
}

bool TargetSnapshot::insert(const Target &target, const QString &plainText, QString *error) const
{
#ifdef SPEECHER_WITH_ATSPI
    if (plainText.isEmpty() || !canInsert(target)) {
        if (error) *error = QStringLiteral("The saved accessible target is no longer safe to edit");
        return false;
    }
    AtspiEditableText *editable = atspi_accessible_get_editable_text(m_accessible.get());
    if (!editable) {
        if (error) *error = QStringLiteral("The saved target does not expose editable text");
        return false;
    }
    const int position = target.selectionStart >= 0 ? target.selectionStart : target.caretOffset;
    const QByteArray utf8 = plainText.toUtf8();
    GError *atspiError = nullptr;
    const bool inserted = atspi_editable_text_insert_text(editable, position, utf8.constData(), utf8.size(), &atspiError);
    if (!inserted && error) {
        *error = atspiError && atspiError->message ? QString::fromUtf8(atspiError->message)
                                                   : QStringLiteral("The target rejected direct text insertion");
    }
    clearError(&atspiError);
    return inserted;
#else
    Q_UNUSED(target)
    Q_UNUSED(plainText)
    if (error) *error = QStringLiteral("AT-SPI support is not available in this build");
    return false;
#endif
}

QString TargetSnapshot::insertionWindow(int insertionOffset, int textLength) const
{
#ifdef SPEECHER_WITH_ATSPI
    if (!m_accessible || !atspi_accessible_is_text(m_accessible.get())) return {};
    AtspiText *text = atspi_accessible_get_text(m_accessible.get());
    if (!text) return {};
    GError *error = nullptr;
    const int count = atspi_text_get_character_count(text, &error);
    clearError(&error);
    if (count < 0) return {};
    const QString value = takeString(atspi_text_get_text(text, qMax(0, insertionOffset - 32),
                                                         qMin(count, insertionOffset + textLength + 32), &error));
    clearError(&error);
    return value;
#else
    Q_UNUSED(insertionOffset)
    Q_UNUSED(textLength)
    return {};
#endif
}

QString TargetSnapshot::correctionWindow(const CorrectionWindow &window) const
{
    const auto &[target, original, prefix, suffix] = window;
#ifdef SPEECHER_WITH_ATSPI
    const int insertionOffset = target.selectionStart >= 0 ? target.selectionStart : target.caretOffset;
    if (!m_accessible || insertionOffset < 0 || !isFocusedText()) return {};
    AtspiText *text = atspi_accessible_get_text(m_accessible.get());
    GError *error = nullptr;
    const int count = atspi_text_get_character_count(text, &error);
    clearError(&error);
    if (count < 0) return {};
    const QString value = takeString(atspi_text_get_text(
        text, qMax(0, insertionOffset - prefix.size() - 16),
        qMin(count, insertionOffset + original.size() + 560 + suffix.size()), &error));
    clearError(&error);
    return value;
#else
    Q_UNUSED(target)
    Q_UNUSED(original)
    Q_UNUSED(prefix)
    Q_UNUSED(suffix)
    return {};
#endif
}

bool TargetSnapshot::isFocusedText() const
{
#ifdef SPEECHER_WITH_ATSPI
    return m_accessible && hasState(m_accessible.get(), ATSPI_STATE_FOCUSED)
        && atspi_accessible_is_text(m_accessible.get());
#else
    return false;
#endif
}

} // namespace speecher::atspi
