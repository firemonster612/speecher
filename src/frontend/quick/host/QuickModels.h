#pragma once

#include "frontend/quick/host/HostSingleton.h"

#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>

namespace speecher {

class ApplicationController;

// The UI's Models singleton: what core knows about the providers that hear
// and clean up dictation, and about running models on this computer. The
// speech and refinement providers come from the registry with where each
// stands (ProviderReadiness) and how each rates (ProviderRatings); the Local
// Model catalog, downloads and Speed Tests, the hardware, the Local Runners,
// the Ollama cleanup-model pull and both Custom Endpoints' connection checks
// come from the controller's LocalSetup, so they outlive the window. Data
// only, as plain values; the UI words it (ModelText). Every list but
// downloads changes through changed(), which fires only when something did.
class QuickModels final : public QObject, public HostSingleton<QuickModels> {
    Q_OBJECT
    QML_NAMED_ELEMENT(Models)
    QML_SINGLETON
    // [{id, label, signsIn, state, problem}] in the registry's order. state
    // is ready, unknown (nothing seen says whether it works, such as a
    // sign-in nobody has checked or a server not yet tested), offline,
    // signedOut, noModel, noRunner, noServer, checkFailed (the server's last
    // connection check failed) or language; problem is core's sentence for
    // any but ready and unknown. Each also has signInChecking and
    // signInCheckFailure, setup's sign-in check apart from the state: whether
    // it runs, and why the last one couldn't reach the service, and
    // languages, the codes core says it listens for (spokenLanguages, without
    // Automatic; a Local Model's are the chosen model's), detects, whether it
    // offers Automatic, and spokenLanguage, the one it listens for in the
    // chain: the Spoken Language as the primary, its own as a fallback
    // (chainSpeechSettings).
    Q_PROPERTY(QVariantList speechServices READ speechServices NOTIFY changed)
    // As speechServices, each with readsScreenshots.
    Q_PROPERTY(QVariantList refinementProviders READ refinementProviders NOTIFY changed)
    // By provider id, for those with one: {bars: [{measure, value}], ratedModel}.
    // measure is accuracy, quality or speed, value out of 10 or null.
    Q_PROPERTY(QVariantMap speechRatings READ speechRatings NOTIFY changed)
    Q_PROPERTY(QVariantMap refinementRatings READ refinementRatings NOTIFY changed)
    // Core's sentence when the chosen speech service can't listen for the
    // Spoken Language, else empty.
    Q_PROPERTY(QString spokenLanguageProblem READ spokenLanguageProblem NOTIFY changed)

    // The probe has answered; until then nothing is suggested, fits or is
    // estimated.
    Q_PROPERTY(bool hardwareKnown READ hardwareKnown NOTIFY changed)
    // {chip, threads, memoryBytes, gpuMemoryBytes, gpu, gpuBackend}, the
    // graphics card being the first the probe found; empty until it answers.
    Q_PROPERTY(QVariantMap computer READ computer NOTIFY changed)
    // [{id, label, available}]: Automatic, the CPU and the backends a card
    // here reaches, and a saved one no card reaches, as unavailable.
    Q_PROPERTY(QVariantList accelerationOptions READ accelerationOptions NOTIFY changed)
    // [{id, label}] the chosen acceleration can run on; label is empty for a
    // saved card that has gone away.
    Q_PROPERTY(QVariantList graphicsCards READ graphicsCards NOTIFY changed)
    // Where the loaded Local Model runs, "NVIDIA GeForce RTX 3060 (CUDA)";
    // empty while none is loaded.
    Q_PROPERTY(QString runningOn READ runningOn NOTIFY changed)
    Q_PROPERTY(QString modelFolder READ modelFolder NOTIFY changed)
    Q_PROPERTY(double modelFolderBytes READ modelFolderBytes NOTIFY changed)

    // The Local Model catalog in its order, each {id, name, rating, bestFor,
    // sizeBytes, fit, werClear, werEveryday, streams, languages,
    // detectsLanguage, licence, fileName, estimatedSeconds, measuredSeconds,
    // bars, pros, cons, problem}. rating is recommended, good, situational
    // or notRecommended; fit fits, tight, tooLarge, or empty until the
    // hardware is known; the word errors are percentages; the seconds are
    // for 10 s of speech, null where there is no figure; problem is the
    // last download's or Speed Test's failure.
    Q_PROPERTY(QVariantList catalog READ catalog NOTIFY changed)
    // Model ids with a finished download, in catalog order.
    Q_PROPERTY(QStringList downloaded READ downloaded NOTIFY changed)
    // Model id to the fraction downloaded, for each download running.
    Q_PROPERTY(QVariantMap downloads READ downloads NOTIFY downloadsChanged)
    // Model ids whose Speed Test runs or waits its turn.
    Q_PROPERTY(QStringList speedTests READ speedTests NOTIFY changed)
    // The model the Local Model service runs: the chosen one, else the
    // suggestion for this computer.
    Q_PROPERTY(QString localModelChoice READ localModelChoice NOTIFY changed)
    // The suggestion for this computer and Spoken Language, once the
    // hardware is known; empty before.
    Q_PROPERTY(QString suggestedModel READ suggestedModel NOTIFY changed)
    // The model dictation uses: empty while another service is chosen or the
    // chosen model isn't downloaded.
    Q_PROPERTY(QString inUse READ inUse NOTIFY changed)

    // Each Custom Endpoint's connection check, for the server the settings
    // name now: {checked, checking, ok, status, models}. status is core's
    // verdict; checked is false before the first check of that server.
    Q_PROPERTY(QVariantMap speechEndpoint READ speechEndpoint NOTIFY changed)
    Q_PROPERTY(QVariantMap refinementEndpoint READ refinementEndpoint NOTIFY changed)

    // The OpenAI and Anthropic models refinement suggests, [{id, label}],
    // the default first.
    Q_PROPERTY(QVariantList openAiModels READ openAiModels CONSTANT)
    Q_PROPERTY(QVariantList anthropicModels READ anthropicModels CONSTANT)
    // Ollama, LM Studio and llama-server: [{id, label, running, models}],
    // label carrying the version of one found running.
    Q_PROPERTY(QVariantList runners READ runners NOTIFY changed)
    Q_PROPERTY(bool detectingRunners READ detectingRunners NOTIFY changed)
    // A look for runners has finished, so one not running really isn't.
    Q_PROPERTY(bool runnersChecked READ runnersChecked NOTIFY changed)
    // The cleanup model suggested for this computer, {tag, name, sizeBytes},
    // or empty where cleanup here would be slower than a cloud provider.
    Q_PROPERTY(QVariantMap cleanupSuggestion READ cleanupSuggestion NOTIFY changed)
    // Ollama is the runner and lacks the suggested cleanup model.
    Q_PROPERTY(bool offerPull READ offerPull NOTIFY changed)
    // The Ollama pull: {tag, completedBytes, totalBytes, running, error}.
    Q_PROPERTY(QVariantMap pull READ pull NOTIFY pullChanged)
    // The rules refinement follows unless a custom prompt replaces them.
    Q_PROPERTY(QString builtInSystemPrompt READ builtInSystemPrompt CONSTANT)

    // The local fallback setup suggests for each role, {modelId, name, joins}
    // or empty, by role: speech and refinement. joins lists the primaries the
    // offer would be added behind; behind any other the chain would drop it.
    Q_PROPERTY(QVariantMap fallbackOffers READ fallbackOffers NOTIFY changed)

public:
    explicit QuickModels(ApplicationController *controller, QObject *parent = nullptr);

    QVariantList speechServices() const { return m_speechServices; }
    QVariantList refinementProviders() const { return m_refinementProviders; }
    QVariantMap speechRatings() const { return m_speechRatings; }
    QVariantMap refinementRatings() const { return m_refinementRatings; }
    QString spokenLanguageProblem() const { return m_spokenLanguageProblem; }
    bool hardwareKnown() const { return m_hardwareKnown; }
    QVariantMap computer() const { return m_computer; }
    QVariantList accelerationOptions() const { return m_accelerationOptions; }
    QVariantList graphicsCards() const { return m_graphicsCards; }
    QString runningOn() const { return m_runningOn; }
    QString modelFolder() const { return m_modelFolder; }
    double modelFolderBytes() const { return m_modelFolderBytes; }
    QVariantList catalog() const { return m_catalog; }
    QStringList downloaded() const { return m_downloaded; }
    QVariantMap downloads() const { return m_downloads; }
    QStringList speedTests() const { return m_speedTests; }
    QString localModelChoice() const { return m_localModelChoice; }
    QString suggestedModel() const { return m_suggestedModel; }
    QString inUse() const { return m_inUse; }
    QVariantMap speechEndpoint() const { return m_speechEndpoint; }
    QVariantMap refinementEndpoint() const { return m_refinementEndpoint; }
    QVariantList openAiModels() const;
    QVariantList anthropicModels() const;
    QVariantList runners() const { return m_runners; }
    bool detectingRunners() const { return m_detectingRunners; }
    bool runnersChecked() const { return m_runnersChecked; }
    QVariantMap cleanupSuggestion() const { return m_cleanupSuggestion; }
    bool offerPull() const { return m_offerPull; }
    QVariantMap pull() const;
    QString builtInSystemPrompt() const;
    QVariantMap fallbackOffers() const { return m_fallbackOffers; }

    // Probes the hardware the first time: what a page that shows fits,
    // suggestions or ratings asks for when it opens. Probing loads the speech
    // engine's backends, so the daemon waits for a window to want it. A page
    // that shows the Local Runners looks for them with detectRunners().
    Q_INVOKABLE void checkComputer();
    // Asks the speech services that sign in whether they are signed in, for
    // setup and the pages that choose a service, where each is otherwise
    // unknown until the first dictation; their state follows the answers.
    Q_INVOKABLE void checkSignIns();
    // {id, label} of a speech service, the id as its label for one this
    // build lacks. Labels never change, so bindings may call it.
    Q_INVOKABLE QVariantMap service(const QString &id) const;
    // Whether an OpenAI model has an Ultrafast tier.
    Q_INVOKABLE bool supportsUltrafast(const QString &model) const;

    // The Local Model catalog's operations, by model id. A download keeps
    // going with the window closed and resumes at the next start; deleting
    // the model dictation uses moves it to another downloaded one. use
    // chooses the model and makes Local Model the speech service.
    Q_INVOKABLE void download(const QString &id);
    Q_INVOKABLE void cancel(const QString &id);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE void use(const QString &id);
    Q_INVOKABLE void testSpeed(const QString &id);
    Q_INVOKABLE void openModelFolder();

    // Checks the Custom Endpoint the settings name now.
    Q_INVOKABLE void testSpeechEndpoint();
    Q_INVOKABLE void testRefinementEndpoint();
    Q_INVOKABLE void detectRunners();
    // Pulls the suggested cleanup model into Ollama, which becomes the
    // runner's model once it is there.
    Q_INVOKABLE void pullCleanupModel();
    Q_INVOKABLE void cancelPull();
    // Adds the local fallback offered for a role, speech or refinement,
    // behind primary, setup's choice for it, which is saved with it. Another
    // role changes nothing and is logged.
    Q_INVOKABLE void acceptFallbackOffer(const QString &role, const QString &primary);

signals:
    void changed();
    void downloadsChanged();
    void pullChanged();

private:
    void refresh();
    void refreshDownloads();

    ApplicationController *m_controller;
    QVariantList m_speechServices;
    QVariantList m_refinementProviders;
    QVariantMap m_speechRatings;
    QVariantMap m_refinementRatings;
    QString m_spokenLanguageProblem;
    bool m_hardwareKnown = false;
    QVariantMap m_computer;
    QVariantList m_accelerationOptions;
    QVariantList m_graphicsCards;
    QString m_runningOn;
    QString m_modelFolder;
    double m_modelFolderBytes = 0;
    QVariantList m_catalog;
    QStringList m_downloaded;
    QVariantMap m_downloads;
    QStringList m_speedTests;
    QString m_localModelChoice;
    QString m_suggestedModel;
    QString m_inUse;
    QVariantMap m_speechEndpoint;
    QVariantMap m_refinementEndpoint;
    QVariantList m_runners;
    bool m_detectingRunners = false;
    bool m_runnersChecked = false;
    QVariantMap m_cleanupSuggestion;
    bool m_offerPull = false;
    QVariantMap m_fallbackOffers;
};

} // namespace speecher
