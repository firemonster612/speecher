#include "core/SecretStore.h"

#include "core/SettingsStore.h"
#include "core/settings/SettingsKeys.h"

#include <QCoreApplication>
#include <QDebug>
#include <QSemaphore>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <tuple>

#ifdef SPEECHER_WITH_QKEYCHAIN
#if __has_include(<qt6keychain/keychain.h>)
#include <qt6keychain/keychain.h>
#else
#include <keychain.h>
#endif
#endif

namespace speecher {

namespace {

constexpr int keyringTimeoutMs = 1500;
constexpr int keyringRetryDelayMs = 5000;

struct SecretEntry {
    SecretStore::Secret secret;
    const char *keyringEntry;
    const QString &settingsKey;
};

// In enum order, one row per Secret.
const SecretEntry secretEntries[] = {
    {SecretStore::Secret::OpenAiApiKey, "openai-api-key", SettingsKeys::OpenAiApiKey},
    {SecretStore::Secret::CliproxyApiKey, "cliproxy-api-key", SettingsKeys::CliproxyApiKey},
    {SecretStore::Secret::SpeechEndpointKey, "speech-endpoint-key", SettingsKeys::SpeechEndpointApiKey},
    {SecretStore::Secret::RefinementEndpointKey, "refinement-endpoint-key", SettingsKeys::RefinementEndpointApiKey},
};
static_assert(std::size(secretEntries) == size_t(SecretStore::Secret::RefinementEndpointKey) + 1);

const SecretEntry &entryFor(SecretStore::Secret secret)
{
    return secretEntries[size_t(secret)];
}

QString pendingDeletionKey(SecretStore::Secret secret)
{
    return SecretStore::settingsKey(secret) + QStringLiteral("PendingDeletion");
}

QString keyringEntry(SecretStore::Secret secret)
{
    return QString::fromLatin1(entryFor(secret).keyringEntry);
}

// Test runs keep their entries apart from a real install's, the way test mode
// already keeps their settings file apart, and so does an isolated instance.
QString keyringService()
{
    return QStandardPaths::isTestModeEnabled() ? QStringLiteral("speecher-tests")
                                               : SettingsKeys::application();
}

// Whether a test's keyring request fails here rather than reaching a
// keyring: always, unless the run provisioned a private keyring
// (tests/keyring/with_session_keyring.sh), so no test reads, writes or
// deletes an entry in the person's own credential store. With a private
// keyring, a test can still fail one kind of request by setting failureVariable.
bool testKeyringRefuses(const char *failureVariable)
{
    return QStandardPaths::isTestModeEnabled()
        && (qEnvironmentVariableIntValue("SPEECHER_TEST_PRIVATE_KEYRING") != 1
            || qEnvironmentVariableIntValue(failureVariable) == 1);
}

} // namespace

QString SecretStore::settingsKey(Secret secret)
{
    return entryFor(secret).settingsKey;
}

SecretStore::SecretStore(SettingsStore *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
}

SecretStore::SecretState &SecretStore::state(Secret secret) const
{
    return m_cache[static_cast<size_t>(secret)];
}

void SecretStore::cacheValue(Secret secret, const QString &value) const
{
    SecretState &entry = state(secret);
    entry.stored = value;
    entry.retryTimer.invalidate();
}

void SecretStore::prefetch()
{
    for (const SecretEntry &row : secretEntries) {
        if (row.secret == Secret::OpenAiApiKey || isSecretKnown(row.secret) || !mayBeInKeyring(row.secret)
            || !settingsFallback(row.secret).isEmpty() || testKeyringRefuses("SPEECHER_TEST_KEYRING_READ_TIMEOUT")) {
            continue;
        }
        startRead(row.secret);
    }
}

void SecretStore::startRead(Secret secret)
{
    SecretState &entry = state(secret);
    // A removal's deletions would come after the read and leave its answer
    // stale.
    if (entry.reading || !m_removals.isEmpty()) {
        return;
    }
    entry.reading = true;
    // Tests that dictation reads the keyring on a worker, not the main thread.
    if (QStandardPaths::isTestModeEnabled()
        && qEnvironmentVariableIntValue("SPEECHER_TEST_KEYRING_REFUSE_GUI_THREAD") == 1) {
        QTimer::singleShot(0, this, [this, secret] {
            finishRead(secret, {false, {}, QStringLiteral("Desktop keyring read on the GUI thread")});
        });
        return;
    }
    askKeyring(this, KeyringRequest::Read, secret, {},
               [this, secret](const KeyringAnswer &answer) { finishRead(secret, answer); });
}

void SecretStore::finishRead(Secret secret, const KeyringAnswer &answer)
{
    SecretState &entry = state(secret);
    entry.reading = false;
    // A value saved or asked for meanwhile is newer than this answer.
    if (isSecretKnown(secret)) {
        return;
    }
    if (!answer.ok) {
        m_lastError = answer.error;
        entry.retryTimer.start();
        return;
    }
    cacheValue(secret, answer.value);
}

QString SecretStore::settingsFallback(Secret secret) const
{
    return m_settings ? m_settings->raw().value(settingsKey(secret)).toString().trimmed() : QString();
}

// Every install has read the OpenAI key from the keyring, so that entry is
// always looked up. The newer entries are read only once Speecher has written
// them, so a locked keyring is not woken for a key nobody saved.
bool SecretStore::mayBeInKeyring(Secret secret) const
{
    return secret == Secret::OpenAiApiKey
        || (m_settings
            && m_settings->raw().value(SettingsKeys::SecretsInKeyring).toStringList()
                   .contains(keyringEntry(secret)));
}

void SecretStore::recordKeyringEntry(Secret secret, bool present) const
{
    if (!m_settings || secret == Secret::OpenAiApiKey) {
        return;
    }
    QStringList entries = m_settings->raw().value(SettingsKeys::SecretsInKeyring).toStringList();
    entries.removeAll(keyringEntry(secret));
    if (present) {
        entries.append(keyringEntry(secret));
    }
    if (entries.isEmpty()) {
        m_settings->raw().remove(SettingsKeys::SecretsInKeyring);
    } else {
        m_settings->raw().setValue(SettingsKeys::SecretsInKeyring, entries);
    }
}

bool SecretStore::deletionPending(Secret secret) const
{
    return m_settings && m_settings->raw().value(pendingDeletionKey(secret)).toBool();
}

bool SecretStore::isSecretKnown(Secret secret) const
{
    const SecretState &entry = state(secret);
    return deletionPending(secret) || entry.stored || entry.writing || entry.waiting;
}

// What a read answers without the keyring: the value asked for last,
// nothing behind a refused deletion's marker, the value in memory, or the
// settings file's copy, which outranks the keyring's. Nothing when only the
// keyring can tell.
std::optional<QString> SecretStore::readWithoutKeyring(Secret secret) const
{
    const SecretState &entry = state(secret);
    if (entry.waiting) return entry.waiting->value;
    if (entry.writing) return entry.writing->value;
    if (deletionPending(secret)) return QString();
    if (entry.stored) return entry.stored;
    // A save that reaches the keyring removes the settings file's copy.
    const QString fallback = settingsFallback(secret);
    if (fallback.isEmpty() && mayBeInKeyring(secret)) return std::nullopt;
    cacheValue(secret, fallback);
    return fallback;
}

QString SecretStore::secret(Secret secret)
{
    if (const std::optional<QString> value = readWithoutKeyring(secret)) return *value;
    SecretState &entry = state(secret);
    if (entry.retryTimer.isValid() && !entry.retryTimer.hasExpired(keyringRetryDelayMs)) return {};
    if (QThread::isMainThread()) {
        startRead(secret);
        return {};
    }
    const KeyringAnswer answer = waitForKeyring(secret);
    m_lastError = answer.error;
    if (!answer.ok) {
        entry.retryTimer.start();
        return {};
    }
    cacheValue(secret, answer.value);
    return answer.value;
}

QString SecretStore::cachedSecret(Secret secret) const
{
    return readWithoutKeyring(secret).value_or(QString());
}

quint64 SecretStore::saveSecret(Secret secret, const QString &value)
{
    return save(secret, {value.trimmed()});
}

void SecretStore::saveSecrets(const QList<std::pair<Secret, QString>> &values)
{
    for (const SecretEntry &row : secretEntries) {
        SecretState &entry = state(row.secret);
        // A write under way, or one this edit asks for, answers itself.
        const bool asked = std::ranges::any_of(values, [&row](const auto &value) { return value.first == row.secret; });
        if (!entry.commitRefused || entry.writing || entry.waiting || entry.keyringBusy || asked
            || !writeRefusal().isEmpty() || !commit(row.secret)) {
            continue;
        }
        entry.commitRefused = false;
        // A refused clear's marker reached the file: still refused.
        if (!deletionPending(row.secret)) {
            emit saveFinished(row.secret, 0, cachedSecret(row.secret).isEmpty(), true, QString());
        }
    }
    for (const auto &[secret, value] : values) {
        saveSecret(secret, value);
    }
}

// Why no save may be written now, or nothing when one may: while Speecher's
// secrets are being removed, and for good once its settings are deleted, so
// neither the keyring nor the settings file gets a key back.
QString SecretStore::writeRefusal() const
{
    if (m_settings && m_settings->raw().deleted()) {
        return QStringLiteral("Speecher's settings were deleted");
    }
    return m_removals.isEmpty() ? QString() : QStringLiteral("Speecher's secrets are being removed");
}

quint64 SecretStore::save(Secret secret, Request request)
{
    request.number = ++m_requests;
    SecretState &entry = state(secret);
    if (const QString refusal = writeRefusal(); !refusal.isEmpty()) {
        // It replaces the requests before it, whose keyring answer then goes
        // unheard.
        entry.writing.reset();
        entry.waiting.reset();
        QTimer::singleShot(0, this, [this, secret, request, refusal] { settle(secret, request, false, refusal); });
        notifyIfWritesSettled();
        return request.number;
    }
    // A clear hides a key a reader could find at once, so no later save of
    // the settings file, with a new server for the key say, goes to disk
    // while the key can still be read.
    if (request.value.isEmpty() && !request.migrating) {
        const bool findable = !settingsFallback(secret).isEmpty() || mayBeInKeyring(secret);
        if (entry.writing || (findable && !holds(secret, request))) {
            hide(secret);
        }
    }
    entry.waiting = request;
    if (!entry.writing && !entry.keyringBusy) {
        writeNext(secret);
    }
    return request.number;
}

// Puts the secret's deletion marker in the settings file and takes out its
// copy there, for the file's next save, until a clear settles.
void SecretStore::hide(Secret secret)
{
    if (!m_settings || (deletionPending(secret) && settingsFallback(secret).isEmpty())) {
        return;
    }
    m_settings->raw().setValue(pendingDeletionKey(secret), true);
    m_settings->raw().remove(settingsKey(secret));
    changedInFile(secret);
}

// Starts writing the request waiting its turn. One that needs no keyring is
// answered from the event loop too, as the keyring's answer would be.
void SecretStore::writeNext(Secret secret)
{
    SecretState &entry = state(secret);
    entry.writing = std::exchange(entry.waiting, std::nullopt);
    entry.keyringBusy = true;
    const auto finish = [this, secret](const std::optional<KeyringAnswer> &answer) {
        state(secret).keyringBusy = false;
        finishWrite(secret, answer);
    };
    const Request &request = *entry.writing;
    if (!writeRefusal().isEmpty() || (!request.migrating && holds(secret, request))) {
        QTimer::singleShot(0, this, [finish] { finish(std::nullopt); });
        return;
    }
    // Clearing a key that was never written to the keyring needs no keyring.
    if (request.value.isEmpty() && !mayBeInKeyring(secret)) {
        QTimer::singleShot(0, this, [finish] { finish(KeyringAnswer{true, {}, {}}); });
        return;
    }
    askKeyring(this, request.value.isEmpty() ? KeyringRequest::Delete : KeyringRequest::Write, secret, request.value,
               finish);
}

// Settles the request being written, from what the keyring answered, or
// nothing when it needed no keyring, then writes the one waiting its turn,
// which replaced it and answers for it.
void SecretStore::finishWrite(Secret secret, const std::optional<KeyringAnswer> &answer)
{
    SecretState &entry = state(secret);
    // A removal dropped the request, and answered for it.
    if (const std::optional<Request> request = std::exchange(entry.writing, std::nullopt)) {
        const bool stored = store(secret, *request, answer);
        if (!entry.waiting) {
            settle(secret, *request, stored, m_lastError);
        }
    }
    // A saveFinished handler may have started the next write already.
    if (entry.waiting && !entry.writing && !entry.keyringBusy) {
        writeNext(secret);
    }
    notifyIfWritesSettled();
}

bool SecretStore::writesSettled() const
{
    return m_removals.isEmpty()
        && std::ranges::none_of(m_cache, [](const SecretState &entry) { return entry.writing || entry.waiting; });
}

void SecretStore::afterWrites(std::function<void()> done)
{
    m_afterWrites = std::move(done);
    notifyIfWritesSettled();
}

void SecretStore::notifyIfWritesSettled()
{
    if (m_afterWrites && writesSettled()) {
        std::exchange(m_afterWrites, {})();
    }
}

// Keeps what the keyring did with the request (keep), or nothing when it
// needed no keyring, and saves the settings file (commit). Returns whether
// storage holds the request, else puts why in lastError().
bool SecretStore::store(Secret secret, const Request &request, const std::optional<KeyringAnswer> &answer)
{
    // Settings deleted while the keyring took it touch neither.
    if (const QString refusal = writeRefusal(); !refusal.isEmpty()) {
        m_lastError = refusal;
        return false;
    }
    // What the settings file tells a reader about the secret.
    const auto fileSays = [this, secret] {
        return std::tuple(settingsFallback(secret), deletionPending(secret), mayBeInKeyring(secret));
    };
    const auto said = fileSays();
    m_lastError.clear();
    const bool kept = !answer || keep(secret, request, *answer);
    // A newer request waiting its turn behind a hidden key, such as a clear
    // or the key entered after it, keeps whatever this older request stored
    // hidden, in the same save of the settings file.
    if (state(secret).waiting && std::get<1>(said)) {
        hide(secret);
    }
    const QString refusal = m_lastError;
    const bool changed = fileSays() != said;
    if (changed) {
        changedInFile(secret);
    }
    const bool committed = commit(secret);
    // A refusal that changed nothing leaves an earlier change's refused
    // commit to report.
    if (kept || changed) {
        state(secret).commitRefused = kept && !committed;
    }
    if (!kept) {
        m_lastError = refusal;
    }
    return kept && committed;
}

// Logs a refusal with why, and tells saveFinished how the request went,
// unless a migration made it.
void SecretStore::settle(Secret secret, const Request &request, bool stored, const QString &error)
{
    if (request.migrating) {
        return;
    }
    const bool cleared = request.value.isEmpty();
    if (!stored) {
        qWarning().noquote() << "Could not" << (cleared ? "clear" : "save") << keyringEntry(secret) + QLatin1Char(':')
                             << error;
    }
    m_lastError = stored ? QString() : error;
    emit saveFinished(secret, request.number, cleared, stored, m_lastError);
}

// Whether storage holds the request where it allows, as this store and a new
// process read it once the settings file saved it (commit), so it needs no
// write: the keyring holds the value (as this store last wrote or read it)
// and the settings file sends readers there, or the file keeps it where the
// request allows. A refused deletion's marker always asks for another try.
bool SecretStore::holds(Secret secret, const Request &request) const
{
    if (state(secret).stored != request.value || deletionPending(secret)) {
        return false;
    }
    // A copy in the settings file outranks the keyring, and won't do for a
    // key only the keyring may keep.
    const QString copy = settingsFallback(secret);
    return copy.isEmpty() ? request.value.isEmpty() || mayBeInKeyring(secret)
                          : request.fallBack && copy == request.value;
}

// Puts what the keyring did with the request in the settings file and this
// store's memory: when it refused, the settings file keeps the key if the
// request allows it, and a refused deletion leaves a marker. Returns whether
// storage holds the request (holds), else puts why in lastError(). A
// migration's refused request leaves both as they were.
bool SecretStore::keep(Secret secret, const Request &request, const KeyringAnswer &answer)
{
    m_lastError = answer.error;
    const QString &value = request.value;
    if (answer.ok) {
        recordKeyringEntry(secret, !value.isEmpty());
        if (m_settings) {
            m_settings->raw().remove(settingsKey(secret));
            m_settings->raw().remove(pendingDeletionKey(secret));
        }
        cacheValue(secret, value);
        return holds(secret, request);
    }
    if (!m_settings || request.migrating) {
        return false;
    }
    if (value.isEmpty()) {
        // Hides the old key across restarts until a deletion goes through,
        // even a key the request keeps out of the settings file: an older
        // copy may be there.
        hide(secret);
        cacheValue(secret, {});
        return false;
    }
    if (!request.fallBack) {
        return false;
    }
    if (QString unsafe; !makeSettingsPrivate(m_settings->raw(), &unsafe)) {
        // Other users could read the key there.
        m_lastError = QStringLiteral("No keyring took it (%1), and %2").arg(m_lastError, unsafe);
        return false;
    }
    m_settings->raw().remove(pendingDeletionKey(secret));
    qWarning().noquote() << "keyring unavailable, keeping" << keyringEntry(secret)
                         << "in the settings file:" << m_lastError;
    m_settings->raw().setValue(settingsKey(secret), value);
    cacheValue(secret, value);
    return holds(secret, request);
}

// Notes that the settings file now says something new about the secret,
// which its next save holds.
void SecretStore::changedInFile(Secret secret)
{
    if (m_settings) {
        state(secret).savedBy = m_settings->raw().saves() + 1;
    }
}

// Whether the settings file saved what it says about the secret, any save of
// the file since its last change counting, else whether it saves now: a key
// is stored only once another process reads it. Returns false, with why in
// lastError(), when the file won't save.
bool SecretStore::commit(Secret secret)
{
    if (!m_settings || m_settings->raw().saves() >= state(secret).savedBy || m_settings->raw().save()) {
        return true;
    }
    m_lastError = QStringLiteral("Could not save the settings file %1").arg(m_settings->raw().fileName());
    return false;
}

QString SecretStore::apiKey()
{
    return secret(Secret::OpenAiApiKey);
}

quint64 SecretStore::saveApiKey(const QString &apiKey)
{
    return save(Secret::OpenAiApiKey, {apiKey.trimmed(), false});
}

QString SecretStore::status()
{
#ifdef SPEECHER_WITH_QKEYCHAIN
    if (!apiKey().isEmpty()) {
        return usesInsecureSettingsFallback()
            ? QStringLiteral("API key saved in Speecher's settings file, not in the keyring")
            : QStringLiteral("API key saved in the desktop keyring");
    }
    if (!m_lastError.isEmpty()) {
        return QStringLiteral("The desktop keyring is not available: %1").arg(m_lastError);
    }
    return QStringLiteral("No API key saved");
#else
    return QStringLiteral("This build cannot save an API key in the desktop keyring");
#endif
}

QString SecretStore::lastError() const
{
    return m_lastError;
}

QString SecretStore::readFailure(Secret secret) const
{
    if (isSecretKnown(secret) || !state(secret).retryTimer.isValid()) {
        return {};
    }
    return QStringLiteral("Desktop keyring unavailable for %1: %2").arg(settingsKey(secret), m_lastError);
}

bool SecretStore::usesInsecureSettingsFallback() const
{
    return !settingsFallback(Secret::OpenAiApiKey).isEmpty();
}

void SecretStore::askKeyring(const QObject *context, KeyringRequest request, Secret secret, const QString &value,
                             KeyringDone done)
{
    const char *failureVariable = request == KeyringRequest::Read ? "SPEECHER_TEST_KEYRING_READ_TIMEOUT"
        : request == KeyringRequest::Write                        ? "SPEECHER_TEST_KEYRING_WRITE_FAILURE"
                                                                  : "SPEECHER_TEST_KEYRING_DELETE_FAILURE";
    if (testKeyringRefuses(failureVariable)) {
        const QString error = request == KeyringRequest::Read ? QStringLiteral("Desktop keyring request timed out")
            : request == KeyringRequest::Write                ? QStringLiteral("Keyring write denied")
                                                              : QStringLiteral("Keyring deletion denied");
        QTimer::singleShot(0, context, [done, error] { done({false, {}, error}); });
        return;
    }
#ifdef SPEECHER_WITH_QKEYCHAIN
    QKeychain::Job *job = nullptr;
    if (request == KeyringRequest::Read) {
        job = new QKeychain::ReadPasswordJob(keyringService());
    } else if (request == KeyringRequest::Write) {
        auto *write = new QKeychain::WritePasswordJob(keyringService());
        write->setTextData(value);
        job = write;
    } else {
        job = new QKeychain::DeletePasswordJob(keyringService());
    }
    job->setKey(keyringEntry(secret));
    // Whichever comes first, the answer or the timeout, is the one done
    // gets. The job deletes itself once it finishes: the backend holds it
    // until then, libsecret past D-Bus's own timeout, and would crash calling
    // back a job deleted sooner.
    const auto answered = std::make_shared<bool>(false);
    QObject::connect(job, &QKeychain::Job::finished, context, [answered, done, job, request] {
        if (std::exchange(*answered, true)) return;
        const QKeychain::Error error = job->error();
        if (error == QKeychain::NoError) {
            done({true, request == KeyringRequest::Read ? static_cast<QKeychain::ReadPasswordJob *>(job)->textData().trimmed()
                                                        : QString(), {}});
        } else if (error == QKeychain::EntryNotFound && request != KeyringRequest::Write) {
            done({true, {}, {}});
        } else {
            done({false, {}, job->errorString()});
        }
    });
    QTimer::singleShot(keyringTimeoutMs, context, [answered, done] {
        if (!std::exchange(*answered, true)) done({false, {}, QStringLiteral("Desktop keyring request timed out")});
    });
    job->start();
#else
    Q_UNUSED(secret);
    Q_UNUSED(value);
    const KeyringAnswer answer = request == KeyringRequest::Write
        ? KeyringAnswer{false, {}, QStringLiteral("QtKeychain support was not compiled in")}
        : KeyringAnswer{true, {}, {}};
    QTimer::singleShot(0, context, [done, answer] { done(answer); });
#endif
}

// Off the main thread: QtKeychain's process-wide executor is not
// thread-safe, so the read starts on the main thread, whose event loop runs
// it, and only this thread waits.
SecretStore::KeyringAnswer SecretStore::waitForKeyring(Secret secret)
{
    if (QStandardPaths::isTestModeEnabled()) {
        const int delay = qEnvironmentVariableIntValue("SPEECHER_TEST_KEYRING_READ_DELAY_MS");
        if (delay > 0) QThread::msleep(delay);
    }
    if (testKeyringRefuses("SPEECHER_TEST_KEYRING_READ_TIMEOUT")) {
        return {false, {}, QStringLiteral("Desktop keyring request timed out")};
    }
    struct Wait {
        QSemaphore answered;
        KeyringAnswer answer;
    };
    const auto wait = std::make_shared<Wait>();
    QCoreApplication *app = QCoreApplication::instance();
    QMetaObject::invokeMethod(app, [app, wait, secret] {
        askKeyring(app, KeyringRequest::Read, secret, {}, [wait](const KeyringAnswer &answer) {
            wait->answer = answer;
            wait->answered.release();
        });
    });
    // The read's own timeout answers first, unless the main thread is busy.
    if (!wait->answered.tryAcquire(1, 2 * keyringTimeoutMs)) {
        return {false, {}, QStringLiteral("Desktop keyring request timed out")};
    }
    return wait->answer;
}

// QtKeychain runs one job at a time, in the order they start, so a write the
// keyring was taking when the removal began lands before its deletions.
void SecretStore::deleteKeyringSecrets(std::function<void(bool deleted, const QString &error)> done)
{
    m_removals.append(std::move(done));
    if (m_removals.size() > 1) {
        return;
    }
    m_removalError.reset();
    // Drops every request under way or waiting its turn, so none is written
    // after the deletions.
    for (const SecretEntry &row : secretEntries) {
        SecretState &entry = state(row.secret);
        std::optional<Request> dropped = std::exchange(entry.waiting, std::nullopt);
        if (std::optional<Request> written = std::exchange(entry.writing, std::nullopt); !dropped) {
            dropped = written;
        }
        if (dropped) {
            settle(row.secret, *dropped, false, writeRefusal());
        }
    }
    removeNext(0);
}

// Deletes the keyring entry of secretEntries[index], then the next.
void SecretStore::removeNext(size_t index)
{
    if (index == std::size(secretEntries)) {
        finishRemoval();
        return;
    }
    const Secret secret = secretEntries[index].secret;
    askKeyring(this, KeyringRequest::Delete, secret, {}, [this, index, secret](const KeyringAnswer &answer) {
        if (answer.ok) {
            recordKeyringEntry(secret, false);
            if (deletionPending(secret)) {
                m_settings->raw().remove(pendingDeletionKey(secret));
                changedInFile(secret);
            }
            removeNext(index + 1);
            return;
        }
        if (!m_removalError) {
            m_removalError = answer.error;
        }
        // Every start reads the OpenAI entry (mayBeInKeyring), so a key the
        // keyring kept would come back after a reinstall; a marker hides it.
        // A keyring that can't be read either, as where there is none, gets
        // no marker, so removal still deletes every setting.
        if (!m_settings || secret != Secret::OpenAiApiKey) {
            removeNext(index + 1);
            return;
        }
        askKeyring(this, KeyringRequest::Read, secret, {}, [this, index, secret](const KeyringAnswer &read) {
            if (read.ok && !read.value.isEmpty()) {
                m_settings->raw().setValue(pendingDeletionKey(secret), true);
                changedInFile(secret);
            }
            removeNext(index + 1);
        });
    });
}

void SecretStore::finishRemoval()
{
    // What storage held is gone, and so is what a read during the deletions
    // found.
    for (SecretState &entry : m_cache) {
        entry.stored.reset();
        entry.retryTimer.invalidate();
        entry.commitRefused = false;
    }
    m_lastError = m_removalError.value_or(QString());
    const bool deleted = !m_removalError;
    for (const auto &done : std::exchange(m_removals, {})) {
        done(deleted, m_lastError);
    }
    notifyIfWritesSettled();
}

QStringList SecretStore::deletionMarkers() const
{
    QStringList markers;
    for (const SecretEntry &row : secretEntries) {
        if (deletionPending(row.secret)) {
            markers.append(pendingDeletionKey(row.secret));
        }
    }
    return markers;
}

// Keys an older Speecher (or a machine without a keyring) left in the
// settings file move into the keyring once one accepts them, and a deletion
// the keyring refused is tried again. CLI Proxy API keys were only ever
// stored in the settings file. Neither reports a refusal: the settings file
// keeps the key, or its deletion marker, as before.
void SecretStore::migrateSettingsFallbacks()
{
    for (const SecretEntry &row : secretEntries) {
        if (deletionPending(row.secret)) {
            save(row.secret, {QString(), true, true});
        } else if (const QString legacy = settingsFallback(row.secret); !legacy.isEmpty()) {
            save(row.secret, {legacy, true, true});
        }
    }
}

} // namespace speecher
