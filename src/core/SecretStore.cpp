#include "core/SecretStore.h"

#include "core/SettingsStore.h"
#include "core/settings/SettingsKeys.h"

#include <QCoreApplication>
#include <QDebug>
#include <QScopedValueRollback>
#include <QStandardPaths>
#include <QThread>

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

#include <QEventLoop>
#include <QTimer>
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

// A refused read stands in for a keyring that does not answer.
bool keyringReadsTimeOut()
{
    return testKeyringRefuses("SPEECHER_TEST_KEYRING_READ_TIMEOUT");
}

// testKeyringRefuses for a request that would wait on the keyring: a real
// keyring answers through the event loop, which runs other work meanwhile.
bool testKeyringRefusesAfterWaiting(const char *failureVariable)
{
    if (!testKeyringRefuses(failureVariable)) {
        return false;
    }
    QCoreApplication::processEvents();
    return true;
}

} // namespace

QString SecretStore::settingsKey(Secret secret)
{
    return entryFor(secret).settingsKey;
}

namespace {

#ifdef SPEECHER_WITH_QKEYCHAIN
template <typename Job>
using KeychainJob = std::unique_ptr<Job, void (*)(Job *)>;

template <typename Job>
KeychainJob<Job> makeKeychainJob()
{
    return KeychainJob<Job>(new Job(keyringService()), [](Job *job) { job->deleteLater(); });
}

// Runs the job, waiting at most keyringTimeoutMs. A job that times out is
// released from `owner`: the caller must not touch it again.
template <typename Job>
std::optional<QKeychain::Error> runKeychainJob(KeychainJob<Job> &owner, QString *error)
{
    Job &job = *owner;
    QEventLoop loop;
    QTimer watchdog;
    bool finished = false;
    job.setAutoDelete(false);
    QObject::connect(&job, &Job::finished, &loop, [&finished, &loop] {
        finished = true;
        loop.quit();
    });
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
    // QtKeychain's process-wide executor is not thread-safe. Its jobs run
    // asynchronously on the GUI thread; only this caller's thread waits.
    // Deferred deletion also keeps the job alive through executor cleanup.
    job.moveToThread(QCoreApplication::instance()->thread());
    job.start();
    if (!finished) {
        watchdog.start(keyringTimeoutMs);
        loop.exec();
    }
    if (!finished) {
        // The backend still holds the job and calls it back when it answers,
        // libsecret only after D-Bus's own timeout; deleting it now would
        // crash that callback. It goes once it finishes instead.
        QObject::connect(&job, &Job::finished, &job, &QObject::deleteLater);
        owner.release();
        if (error) {
            *error = QStringLiteral("Desktop keyring request timed out");
        }
        return std::nullopt;
    }
    if (job.error() != QKeychain::NoError && error) {
        *error = job.errorString();
    }
    return job.error();
}
#endif

} // namespace

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
#ifdef SPEECHER_WITH_QKEYCHAIN
    for (const SecretEntry &row : secretEntries) {
        if (row.secret == Secret::OpenAiApiKey || isSecretKnown(row.secret) || !mayBeInKeyring(row.secret)
            || !settingsFallback(row.secret).isEmpty() || keyringReadsTimeOut()) {
            continue;
        }
        auto *job = new QKeychain::ReadPasswordJob(keyringService(), this);
        job->setKey(keyringEntry(row.secret));
        connect(job, &QKeychain::Job::finished, this,
                [this, job, secret = row.secret, requests = state(row.secret).requests] {
            // A value saved, asked for or read meanwhile is newer than this answer.
            const SecretState &entry = state(secret);
            if (entry.stored || entry.requests != requests) return;
            if (job->error() == QKeychain::NoError) {
                cacheValue(secret, job->textData().trimmed());
            } else if (job->error() == QKeychain::EntryNotFound) {
                cacheValue(secret, settingsFallback(secret));
            }
        });
        job->start();
    }
#endif
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
    return deletionPending(secret) || entry.stored || entry.requested;
}

// What a read answers without the keyring: the value asked for, nothing
// behind a refused deletion's marker, the value in memory, or the settings
// file's copy, which outranks the keyring's. Nothing when only the keyring
// can tell.
std::optional<QString> SecretStore::readWithoutKeyring(Secret secret) const
{
    const SecretState &entry = state(secret);
    if (entry.requested) return entry.requested->value;
    if (deletionPending(secret)) return QString();
    if (entry.stored) return entry.stored;
    // A save that reaches the keyring removes the settings file's copy.
    const QString fallback = settingsFallback(secret);
    if (fallback.isEmpty() && mayBeInKeyring(secret)) return std::nullopt;
    cacheValue(secret, fallback);
    return fallback;
}

QString SecretStore::secret(Secret secret) const
{
    SecretState &entry = state(secret);
    for (;;) {
        if (const std::optional<QString> value = readWithoutKeyring(secret)) return *value;
        if (entry.retryTimer.isValid() && !entry.retryTimer.hasExpired(keyringRetryDelayMs)) return {};
        const quint64 requests = entry.requests;
        m_lastError.clear();
        const QString value = keyringSecret(secret);
        // A save asked for during the keyring's wait is newer than its answer.
        if (entry.requests != requests) continue;
        if (!m_lastError.isEmpty()) {
            entry.retryTimer.start();
            return {};
        }
        cacheValue(secret, value);
        return value;
    }
}

QString SecretStore::cachedSecret(Secret secret) const
{
    return readWithoutKeyring(secret).value_or(QString());
}

SecretStore::SaveResult SecretStore::saveSecret(Secret secret, const QString &value)
{
    return save(secret, {value.trimmed()});
}

quint64 SecretStore::nextRequest(Secret secret) const
{
    return state(secret).requests + 1;
}

void SecretStore::saveSecrets(const QList<std::pair<Secret, QString>> &values)
{
    QList<quint64> requests;
    for (const auto &[secret, value] : values) {
        requests.append(ask(secret, {value.trimmed()}));
    }
    for (qsizetype i = 0; i < values.size(); ++i) {
        const Secret secret = values.at(i).first;
        const SecretState &entry = state(secret);
        // A newer request, the write under way or a removal settles this one.
        if (entry.requests != requests.at(i) || entry.writing || !entry.requested) {
            continue;
        }
        settle(secret, requests.at(i));
    }
}

// Why no save may be written now, or nothing when one may: while Speecher's
// secrets are being removed, a save waiting its turn when the removal began
// included, and for good once its settings are deleted, so neither the
// keyring nor the settings file gets a key back.
QString SecretStore::writeRefusal() const
{
    if (m_settings && m_settings->raw().deleted()) {
        return QStringLiteral("Speecher's settings were deleted");
    }
    return m_removing ? QStringLiteral("Speecher's secrets are being removed") : QString();
}

// Makes the request the secret's requested state, which every read returns
// until it settles, and returns its number. When no save may be written
// (writeRefusal), it refuses the request unwritten instead.
quint64 SecretStore::ask(Secret secret, const Request &request)
{
    SecretState &entry = state(secret);
    const quint64 number = ++entry.requests;
    if (const QString refusal = writeRefusal(); !refusal.isEmpty()) {
        refuse(secret, number, request.value.isEmpty(), refusal);
    } else {
        entry.requested = request;
    }
    return number;
}

SecretStore::SaveResult SecretStore::save(Secret secret, const Request &request)
{
    const quint64 number = ask(secret, request);
    if (const QString refusal = writeRefusal(); !refusal.isEmpty()) {
        m_lastError = refusal;
        return SaveResult::Refused;
    }
    // The write under way writes this next.
    if (state(secret).writing) {
        return SaveResult::Pending;
    }
    return settle(secret, number);
}

// Writes the secret's requested state, and again after each write during
// whose wait a newer request came, until one finishes with none. Returns how
// request number own went, or Pending when a newer one replaced it or a
// removal of Speecher's secrets dropped it. Logs a refusal with why.
SecretStore::SaveResult SecretStore::settle(Secret secret, quint64 own)
{
    SecretState &entry = state(secret);
    entry.writing = true;
    quint64 written = 0;
    Request request;
    bool stored = false;
    do {
        written = entry.requests;
        request = *entry.requested;
        stored = holds(secret, request) || write(secret, request);
    } while (entry.requested && entry.requests != written);
    entry.writing = false;
    // The removal already answered for what was asked.
    if (!entry.requested) {
        return SaveResult::Pending;
    }
    entry.requested.reset();
    const bool cleared = request.value.isEmpty();
    const QString error = m_lastError;
    if (stored) {
        emit saveFinished(secret, written, cleared, true, QString());
    } else {
        refuse(secret, written, cleared, error);
    }
    // A saveFinished handler may have saved another secret, which sets lastError().
    m_lastError = error;
    if (written != own) {
        return SaveResult::Pending;
    }
    return stored ? SaveResult::Stored : SaveResult::Refused;
}

// Logs that request number of the secret was refused for error's reason, and
// tells saveFinished.
void SecretStore::refuse(Secret secret, quint64 request, bool cleared, const QString &error)
{
    qWarning().noquote() << "Could not" << (cleared ? "clear" : "save") << keyringEntry(secret) + QLatin1Char(':')
                         << error;
    emit saveFinished(secret, request, cleared, false, error);
}

// Whether storage holds the request where it allows, as this store and a new
// process read it, so it needs no write: the keyring holds the value (as this
// store last wrote or read it) and the settings file sends readers there, or
// the file keeps it where the request allows. A refused deletion's marker
// always asks for another try, and so does a change the settings file may
// not have saved: holds saves it, with why it won't in lastError().
bool SecretStore::holds(Secret secret, const Request &request) const
{
    SecretState &entry = state(secret);
    if (entry.stored != request.value || deletionPending(secret)) {
        return false;
    }
    // A copy in the settings file outranks the keyring, and won't do for a
    // key only the keyring may keep.
    const QString copy = settingsFallback(secret);
    const bool found = copy.isEmpty() ? request.value.isEmpty() || mayBeInKeyring(secret)
                                      : request.fallBack && copy == request.value;
    if (!found || (entry.unsaved && !saveSettingsFile())) {
        return false;
    }
    entry.unsaved = false;
    return true;
}

// Writes the request to the keyring or, when the keyring refuses and the
// request allows it, to the settings file. Returns whether storage holds it
// (holds).
bool SecretStore::write(Secret secret, const Request &request)
{
    m_lastError.clear();
    // A write resumed after the settings were deleted touches neither.
    if (const QString refusal = writeRefusal(); !refusal.isEmpty()) {
        m_lastError = refusal;
        return false;
    }
    // What the settings file tells a reader about the secret. A write that
    // leaves it as it was has nothing the file must save, so a file that
    // won't save doesn't refuse it.
    const auto fileSays = [this, secret] {
        return std::tuple(settingsFallback(secret), deletionPending(secret), mayBeInKeyring(secret));
    };
    const auto said = fileSays();
    const quint64 removals = m_removals;
    const QString &value = request.value;
    // Clearing a key that was never written to the keyring needs no keyring.
    const bool inKeyring = value.isEmpty() ? !mayBeInKeyring(secret) || deleteKeyringSecret(secret)
                                           : writeKeyringSecret(secret, value);
    // A removal during the wait dropped this request, and its deletions came
    // after whatever the keyring took.
    if (m_removals != removals) {
        return false;
    }
    bool kept = true;
    if (inKeyring) {
        if (!value.isEmpty()) {
            recordKeyringEntry(secret, true);
        }
        if (m_settings) {
            m_settings->raw().remove(settingsKey(secret));
            m_settings->raw().remove(pendingDeletionKey(secret));
        }
        cacheValue(secret, value);
    } else if (m_settings && value.isEmpty()) {
        // Hides the old key across restarts until a deletion goes through,
        // even a key the request keeps out of the settings file: an older
        // copy may be there.
        m_settings->raw().setValue(pendingDeletionKey(secret), true);
        m_settings->raw().remove(settingsKey(secret));
        m_settings->raw().sync();
        cacheValue(secret, {});
        kept = false;
    } else if (m_settings && request.fallBack) {
        m_settings->raw().remove(pendingDeletionKey(secret));
        qWarning().noquote() << "keyring unavailable, keeping" << keyringEntry(secret)
                             << "in the settings file:" << m_lastError;
        m_settings->raw().setValue(settingsKey(secret), value);
        cacheValue(secret, value);
    } else {
        kept = false;
    }
    if (fileSays() != said) {
        state(secret).unsaved = true;
    }
    return kept && holds(secret, request);
}

// Saves the settings file now: a key is stored only once another process
// reads it. Returns false, with why in lastError(), when the file won't save.
bool SecretStore::saveSettingsFile() const
{
    if (!m_settings || m_settings->raw().save()) {
        return true;
    }
    m_lastError = QStringLiteral("Could not save the settings file %1").arg(m_settings->raw().fileName());
    return false;
}

QString SecretStore::apiKey() const
{
    return secret(Secret::OpenAiApiKey);
}

SecretStore::SaveResult SecretStore::saveApiKey(const QString &apiKey)
{
    return save(Secret::OpenAiApiKey, {apiKey.trimmed(), false});
}

QString SecretStore::status() const
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

QString SecretStore::keyringSecret(Secret secret) const
{
    if (QStandardPaths::isTestModeEnabled()) {
        const int delay = qEnvironmentVariableIntValue("SPEECHER_TEST_KEYRING_READ_DELAY_MS");
        if (delay > 0) QThread::msleep(delay);
        // Tests that dictation reads the keyring on a worker, not the GUI thread.
        if (qEnvironmentVariableIntValue("SPEECHER_TEST_KEYRING_REFUSE_GUI_THREAD") == 1
            && QThread::isMainThread()) {
            m_lastError = QStringLiteral("Desktop keyring read on the GUI thread");
            return {};
        }
    }
    if (testKeyringRefusesAfterWaiting("SPEECHER_TEST_KEYRING_READ_TIMEOUT")) {
        m_lastError = QStringLiteral("Desktop keyring request timed out");
        return {};
    }
#ifdef SPEECHER_WITH_QKEYCHAIN
    auto job = makeKeychainJob<QKeychain::ReadPasswordJob>();
    job->setKey(keyringEntry(secret));
    QString error;
    const auto result = runKeychainJob(job, &error);
    if (result == QKeychain::NoError) {
        m_lastError.clear();
        return job->textData().trimmed();
    }
    if (result == QKeychain::EntryNotFound) {
        m_lastError.clear();
        return {};
    }
    m_lastError = error;
#else
    Q_UNUSED(secret);
#endif
    return {};
}

bool SecretStore::writeKeyringSecret(Secret secret, const QString &value) const
{
    if (testKeyringRefusesAfterWaiting("SPEECHER_TEST_KEYRING_WRITE_FAILURE")) {
        m_lastError = QStringLiteral("Keyring write denied");
        return false;
    }
#ifdef SPEECHER_WITH_QKEYCHAIN
    auto job = makeKeychainJob<QKeychain::WritePasswordJob>();
    job->setKey(keyringEntry(secret));
    job->setTextData(value);
    return runKeychainJob(job, &m_lastError) == QKeychain::NoError;
#else
    Q_UNUSED(secret);
    Q_UNUSED(value);
    m_lastError = QStringLiteral("QtKeychain support was not compiled in");
    return false;
#endif
}

bool SecretStore::deleteKeyringSecret(Secret secret) const
{
    if (testKeyringRefusesAfterWaiting("SPEECHER_TEST_KEYRING_DELETE_FAILURE")) {
        m_lastError = QStringLiteral("Keyring deletion denied");
        return false;
    }
#ifdef SPEECHER_WITH_QKEYCHAIN
    auto job = makeKeychainJob<QKeychain::DeletePasswordJob>();
    job->setKey(keyringEntry(secret));
    QString error;
    const auto result = runKeychainJob(job, &error);
    if (result == QKeychain::NoError || result == QKeychain::EntryNotFound) {
        m_lastError.clear();
        recordKeyringEntry(secret, false);
        return true;
    }
    m_lastError = error;
    return false;
#else
    recordKeyringEntry(secret, false);
    m_lastError.clear();
    return true;
#endif
}

// QtKeychain runs one job at a time, in the order they start, so a write the
// keyring was taking when the removal began lands before its deletions.
bool SecretStore::deleteKeyringSecrets()
{
    // A removal started during this one's keyring wait leaves saves refused
    // when it returns, until this one does.
    const QScopedValueRollback removing(m_removing, true);
    ++m_removals;
    // Drops every request under way or waiting its turn, so none is written
    // after the deletions.
    for (const SecretEntry &row : secretEntries) {
        SecretState &entry = state(row.secret);
        const quint64 dropped = entry.requests;
        // A keyring read under way drops its answer, as for a request.
        ++entry.requests;
        if (const std::optional<Request> request = std::exchange(entry.requested, std::nullopt)) {
            refuse(row.secret, dropped, request->value.isEmpty(), writeRefusal());
        }
    }
    bool ok = true;
    QString firstError;
    for (const SecretEntry &row : secretEntries) {
        if (!deleteKeyringSecret(row.secret) && ok) {
            ok = false;
            firstError = m_lastError;
        }
    }
    m_lastError = firstError;
    // What storage held is gone, and so is what a read during the deletions
    // found.
    for (SecretState &entry : m_cache) {
        entry.stored.reset();
        entry.retryTimer.invalidate();
    }
    return ok;
}

// Keys an older Speecher (or a machine without a keyring) left in the
// settings file move into the keyring once one accepts them. CLI Proxy API
// keys were only ever stored there. What this changes in the settings file
// is saved with the next save that goes through, the secret's own included.
void SecretStore::migrateSettingsFallbacks()
{
    for (const SecretEntry &row : secretEntries) {
        SecretState &entry = state(row.secret);
        if (deletionPending(row.secret)) {
            if (deleteKeyringSecret(row.secret)) {
                m_settings->raw().remove(pendingDeletionKey(row.secret));
                entry.unsaved = true;
            }
            cacheValue(row.secret, {});
            continue;
        }
        const QString legacy = settingsFallback(row.secret);
        if (legacy.isEmpty()) {
            continue;
        }
        m_lastError.clear();
        if (writeKeyringSecret(row.secret, legacy)) {
            recordKeyringEntry(row.secret, true);
            m_settings->raw().remove(row.settingsKey);
            entry.unsaved = true;
            // The value is in hand, so this run never needs to read it back.
            cacheValue(row.secret, legacy);
        }
    }
}

} // namespace speecher
