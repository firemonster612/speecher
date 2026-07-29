#include "platform/AtSpiTargetProvider.h"

#include "core/LearnedCorrection.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QThread>
#include <QTimer>

#ifdef SPEECHER_WITH_ATSPI
#include <atspi/atspi.h>
#endif

namespace speecher {

#ifdef SPEECHER_WITH_ATSPI
namespace {

constexpr int contextCharacters = 240;
constexpr int maximumVisitedObjects = 4000;
constexpr int maximumTreeDepth = 40;

QString takeString(gchar *value)
{
    const QString result = QString::fromUtf8(value ? value : "");
    g_free(value);
    return result;
}

void clearError(GError **error)
{
    if (error && *error) {
        g_error_free(*error);
        *error = nullptr;
    }
}

bool hasState(AtspiAccessible *object, AtspiStateType state)
{
    AtspiStateSet *states = object ? atspi_accessible_get_state_set(object) : nullptr;
    const bool present = states && atspi_state_set_contains(states, state);
    if (states) {
        g_object_unref(states);
    }
    return present;
}

AtspiAccessible *focusedObject(AtspiAccessible *object, int depth, int *visited)
{
    if (!object || depth > maximumTreeDepth || ++(*visited) > maximumVisitedObjects) {
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
        AtspiAccessible *focused = focusedObject(child, depth + 1, visited);
        g_object_unref(child);
        if (focused) {
            return focused;
        }
    }

    return hasState(object, ATSPI_STATE_FOCUSED)
        ? ATSPI_ACCESSIBLE(g_object_ref(object))
        : nullptr;
}

AtspiAccessible *focusedObjectInActiveWindow(AtspiAccessible *desktop)
{
    if (!desktop) {
        return nullptr;
    }
    GError *error = nullptr;
    const int applicationCount = atspi_accessible_get_child_count(desktop, &error);
    clearError(&error);
    for (int applicationIndex = 0; applicationIndex < applicationCount; ++applicationIndex) {
        AtspiAccessible *application = atspi_accessible_get_child_at_index(desktop, applicationIndex, &error);
        clearError(&error);
        if (!application) {
            continue;
        }

        if (hasState(application, ATSPI_STATE_ACTIVE)) {
            int visited = 0;
            AtspiAccessible *focused = focusedObject(application, 0, &visited);
            g_object_unref(application);
            if (focused) {
                return focused;
            }
            continue;
        }

        const int windowCount = atspi_accessible_get_child_count(application, &error);
        clearError(&error);
        for (int windowIndex = 0; windowIndex < windowCount; ++windowIndex) {
            AtspiAccessible *window = atspi_accessible_get_child_at_index(application, windowIndex, &error);
            clearError(&error);
            if (!window) {
                continue;
            }
            if (hasState(window, ATSPI_STATE_ACTIVE)) {
                int visited = 0;
                AtspiAccessible *focused = focusedObject(window, 0, &visited);
                g_object_unref(window);
                g_object_unref(application);
                if (focused) {
                    return focused;
                }
                return nullptr;
            }
            g_object_unref(window);
        }
        g_object_unref(application);
    }
    return nullptr;
}

QString applicationAttribute(AtspiAccessible *application)
{
    GError *error = nullptr;
    GHashTable *attributes = atspi_accessible_get_attributes(application, &error);
    clearError(&error);
    if (!attributes) {
        return {};
    }
    QString result;
    for (const char *key : {"desktop-entry", "application-id", "id"}) {
        if (const auto *value = static_cast<const char *>(g_hash_table_lookup(attributes, key))) {
            result = QString::fromUtf8(value).trimmed();
            if (!result.isEmpty()) {
                break;
            }
        }
    }
    g_hash_table_unref(attributes);
    return result;
}

QString processName(qint64 processId)
{
    QFile file(QStringLiteral("/proc/%1/comm").arg(processId));
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

void populateText(Target *target, AtspiAccessible *object)
{
    if (!target || !atspi_accessible_is_text(object)) {
        return;
    }
    AtspiText *text = atspi_accessible_get_text(object);
    if (!text) {
        return;
    }

    GError *error = nullptr;
    const int count = atspi_text_get_character_count(text, &error);
    clearError(&error);
    const int caret = atspi_text_get_caret_offset(text, &error);
    clearError(&error);
    if (count < 0 || caret < 0) {
        return;
    }

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
            g_free(selection);
        }
    } else {
        clearError(&error);
    }
}

QString targetFingerprint(const Target &target)
{
    const QByteArray material = target.applicationId.toUtf8()
        + '\0' + QByteArray::number(target.processId)
        + '\0' + QByteArray::number(target.caretOffset)
        + '\0' + target.nearbyTextBefore.toUtf8()
        + '\0' + target.nearbyTextAfter.toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(material, QCryptographicHash::Sha256).toHex());
}

} // namespace
#endif

AtSpiTargetProvider::~AtSpiTargetProvider()
{
    clearAccessible();
}

void AtSpiTargetProvider::clearAccessible()
{
    ++m_observationGeneration;
#ifdef SPEECHER_WITH_ATSPI
    if (m_accessible) {
        g_object_unref(ATSPI_ACCESSIBLE(m_accessible));
    }
#endif
    m_accessible = nullptr;
    m_snapshot = {};
}

Target AtSpiTargetProvider::capture()
{
    clearAccessible();
    Target target;
#ifdef SPEECHER_WITH_ATSPI
    if (!atspi_is_initialized() && atspi_init() != 0) {
        return target;
    }

    for (int desktopIndex = 0; desktopIndex < atspi_get_desktop_count(); ++desktopIndex) {
        AtspiAccessible *desktop = atspi_get_desktop(desktopIndex);
        AtspiAccessible *focused = focusedObjectInActiveWindow(desktop);
        if (!focused) {
            int visited = 0;
            focused = focusedObject(desktop, 0, &visited);
        }
        if (desktop) {
            g_object_unref(desktop);
        }
        if (!focused) {
            continue;
        }
        GError *error = nullptr;
        AtspiAccessible *application = atspi_accessible_get_application(focused, &error);
        clearError(&error);
        target.applicationName = application ? takeString(atspi_accessible_get_name(application, &error)) : QString();
        clearError(&error);
        target.applicationId = application ? applicationAttribute(application) : QString();
        target.processId = application ? atspi_accessible_get_process_id(application, &error) : 0;
        clearError(&error);
        target.processName = processName(target.processId);
        if (target.applicationId.isEmpty()) {
            target.applicationId = target.processName.toLower();
        }
        target.controlName = takeString(atspi_accessible_get_name(focused, &error));
        clearError(&error);
        target.role = takeString(atspi_accessible_get_role_name(focused, &error));
        clearError(&error);
        target.toolkit = application ? takeString(atspi_accessible_get_toolkit_name(application, &error)) : QString();
        clearError(&error);
        target.secure = atspi_accessible_get_role(focused, &error) == ATSPI_ROLE_PASSWORD_TEXT;
        clearError(&error);
        target.accessible = true;
        if (!target.secure) {
            populateText(&target, focused);
        }
        target.category = classifyTarget(target);
        target.fingerprint = targetFingerprint(target);

        if (application) {
            g_object_unref(application);
        }
        m_accessible = focused;
        m_snapshot = target;
        break;
    }
#endif
    return target;
}

bool AtSpiTargetProvider::stillFocused(const Target &target)
{
    return matchesSnapshot(target, true);
}

bool AtSpiTargetProvider::matchesSnapshot(const Target &target, bool requireFocus) const
{
#ifdef SPEECHER_WITH_ATSPI
    if (!m_accessible || target.secure || target.fingerprint != m_snapshot.fingerprint) {
        return false;
    }
    AtspiAccessible *accessible = ATSPI_ACCESSIBLE(m_accessible);
    AtspiStateSet *states = atspi_accessible_get_state_set(accessible);
    const bool focused = states && atspi_state_set_contains(states, ATSPI_STATE_FOCUSED);
    const bool defunct = !states || atspi_state_set_contains(states, ATSPI_STATE_DEFUNCT);
    if (states) {
        g_object_unref(states);
    }
    if (defunct || (requireFocus && !focused)) {
        return false;
    }

    Target current = m_snapshot;
    current.nearbyTextBefore.clear();
    current.nearbyTextAfter.clear();
    current.caretOffset = -1;
    current.selectionStart = -1;
    current.selectionEnd = -1;
    populateText(&current, accessible);
    current.fingerprint = targetFingerprint(current);
    return current.fingerprint == target.fingerprint;
#else
    Q_UNUSED(target)
    return false;
#endif
}

bool AtSpiTargetProvider::canInsertText(const Target &target)
{
#ifdef SPEECHER_WITH_ATSPI
    if (!m_accessible
        || target.secure
        || target.caretOffset < 0
        || (target.selectionStart >= 0 && target.selectionEnd > target.selectionStart)) {
        return false;
    }
    AtspiAccessible *accessible = ATSPI_ACCESSIBLE(m_accessible);
    return atspi_accessible_is_editable_text(accessible)
        && matchesSnapshot(target, false);
#else
    Q_UNUSED(target)
    return false;
#endif
}

bool AtSpiTargetProvider::insertText(const Target &target, const QString &plainText, QString *error)
{
#ifdef SPEECHER_WITH_ATSPI
    if (plainText.isEmpty() || !canInsertText(target)) {
        if (error) {
            *error = QStringLiteral("The saved accessible target is no longer safe to edit");
        }
        return false;
    }

    AtspiEditableText *editable = atspi_accessible_get_editable_text(ATSPI_ACCESSIBLE(m_accessible));
    if (!editable) {
        if (error) {
            *error = QStringLiteral("The saved target does not expose editable text");
        }
        return false;
    }

    const int position = target.selectionStart >= 0 ? target.selectionStart : target.caretOffset;
    const QByteArray utf8 = plainText.toUtf8();
    GError *atspiError = nullptr;
    const bool inserted = atspi_editable_text_insert_text(
        editable,
        position,
        utf8.constData(),
        utf8.size(),
        &atspiError);
    if (!inserted && error) {
        *error = atspiError && atspiError->message
            ? QString::fromUtf8(atspiError->message)
            : QStringLiteral("The target rejected direct text insertion");
    }
    clearError(&atspiError);
    return inserted;
#else
    Q_UNUSED(target)
    Q_UNUSED(plainText)
    if (error) {
        *error = QStringLiteral("AT-SPI support is not available in this build");
    }
    return false;
#endif
}

bool AtSpiTargetProvider::verifyInsertion(const Target &target, const QString &plainText)
{
#ifdef SPEECHER_WITH_ATSPI
    if (!m_accessible
        || target.secure
        || target.fingerprint != m_snapshot.fingerprint
        || plainText.isEmpty()) {
        return false;
    }
    AtspiAccessible *accessible = ATSPI_ACCESSIBLE(m_accessible);
    if (!atspi_accessible_is_text(accessible)) {
        return false;
    }
    AtspiText *text = atspi_accessible_get_text(accessible);
    const int insertionOffset = target.selectionStart >= 0 ? target.selectionStart : target.caretOffset;
    if (!text || insertionOffset < 0) {
        return false;
    }

    for (int attempt = 0; attempt < 5; ++attempt) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (attempt > 0) {
            QThread::msleep(40);
        }
        GError *error = nullptr;
        const int count = atspi_text_get_character_count(text, &error);
        clearError(&error);
        if (count < 0) {
            continue;
        }
        const int start = qMax(0, insertionOffset - 32);
        const int end = qMin(count, insertionOffset + plainText.size() + 32);
        const QString nearby = takeString(atspi_text_get_text(text, start, end, &error));
        clearError(&error);
        const int insertedAt = nearby.indexOf(plainText);
        if (insertedAt >= 0) {
            const QString prefix = nearby.left(insertedAt).right(24);
            const QString suffix = nearby.mid(insertedAt + plainText.size()).left(24);
            if (prefix.size() >= 8 && suffix.size() >= 8) {
                const quint64 generation = ++m_observationGeneration;
                QTimer::singleShot(6500, this, [this, target, plainText, prefix, suffix, generation] {
                    observeCorrection(target, plainText, prefix, suffix, generation);
                });
            }
            return true;
        }
    }
#else
    Q_UNUSED(target)
    Q_UNUSED(plainText)
#endif
    return false;
}

void AtSpiTargetProvider::observeCorrection(const Target &target,
                                            const QString &original,
                                            const QString &prefix,
                                            const QString &suffix,
                                            quint64 generation)
{
#ifdef SPEECHER_WITH_ATSPI
    if (generation != m_observationGeneration
        || !m_accessible
        || target.secure
        || original.isEmpty()) {
        return;
    }
    AtspiAccessible *accessible = ATSPI_ACCESSIBLE(m_accessible);
    AtspiStateSet *states = atspi_accessible_get_state_set(accessible);
    const bool focused = states && atspi_state_set_contains(states, ATSPI_STATE_FOCUSED);
    if (states) {
        g_object_unref(states);
    }
    if (!focused || !atspi_accessible_is_text(accessible)) {
        return;
    }

    AtspiText *text = atspi_accessible_get_text(accessible);
    GError *error = nullptr;
    const int count = atspi_text_get_character_count(text, &error);
    clearError(&error);
    const int insertionOffset = target.selectionStart >= 0 ? target.selectionStart : target.caretOffset;
    if (count < 0 || insertionOffset < 0) {
        return;
    }
    const int start = qMax(0, insertionOffset - prefix.size() - 16);
    const int end = qMin(count, insertionOffset + original.size() + 560 + suffix.size());
    const QString window = takeString(atspi_text_get_text(text, start, end, &error));
    clearError(&error);

    const std::optional<QString> corrected = correctionBetweenAnchors(
        window,
        prefix,
        suffix,
        original);
    if (!corrected) {
        return;
    }
    emit correctionObserved(original, *corrected, target.applicationId, 0.92);
#else
    Q_UNUSED(target)
    Q_UNUSED(original)
    Q_UNUSED(prefix)
    Q_UNUSED(suffix)
    Q_UNUSED(generation)
#endif
}

} // namespace speecher
