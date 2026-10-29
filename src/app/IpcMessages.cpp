#include "app/IpcMessages.h"

#include "core/settings/SpokenLanguages.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <utility>

namespace speecher {

namespace {

constexpr std::pair<IpcCommand, const char *> kCommandNames[] = {
    {IpcCommand::Toggle, "toggle"},
    {IpcCommand::Start, "start"},
    {IpcCommand::Stop, "stop"},
    {IpcCommand::Cancel, "cancel"},
    {IpcCommand::ShowMain, "showMain"},
    {IpcCommand::ShowSettings, "showSettings"},
    {IpcCommand::ShowSetup, "showSetup"},
    {IpcCommand::Transcribe, "transcribe"},
    {IpcCommand::RecordStart, "recordStart"},
    {IpcCommand::RecordStatus, "recordStatus"},
    {IpcCommand::RecordStop, "recordStop"},
    {IpcCommand::Status, "status"},
    {IpcCommand::WatchStatus, "watchStatus"},
    {IpcCommand::Last, "last"},
    {IpcCommand::Providers, "providers"},
    {IpcCommand::AddVocabulary, "addVocabulary"},
    {IpcCommand::Quit, "quit"},
#ifdef SPEECHER_E2E_HOOKS
    {IpcCommand::E2eUpdateCheck, "e2eUpdateCheck"},
    {IpcCommand::E2eUpdateStatus, "e2eUpdateStatus"},
    {IpcCommand::E2ePause, "e2ePause"},
    {IpcCommand::E2eResume, "e2eResume"},
    {IpcCommand::E2eUpdateAccept, "e2eUpdateAccept"},
#endif
};

// Older clients compare a refusal's message against this to tell an
// instance older than their command, so it never changes.
const QString kUnknownCommandMessage = QStringLiteral("Unknown command");

// A JSON array of strings, or nothing when value is something else. An
// absent field reads as an empty list.
std::optional<QStringList> stringList(const QJsonValue &value)
{
    if (value.isUndefined()) {
        return QStringList();
    }
    if (!value.isArray()) {
        return std::nullopt;
    }
    QStringList strings;
    for (const QJsonValue &item : value.toArray()) {
        if (!item.isString()) {
            return std::nullopt;
        }
        strings << item.toString();
    }
    return strings;
}

std::optional<QJsonObject> jsonObject(const QByteArray &frame)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(frame, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }
    return document.object();
}

QByteArray frame(const QJsonObject &object)
{
    QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    bytes.append('\n');
    return bytes;
}

} // namespace

QString ipcCommandName(IpcCommand command)
{
    for (const auto &[candidate, name] : kCommandNames) {
        if (candidate == command) {
            return QLatin1String(name);
        }
    }
    Q_UNREACHABLE_RETURN(QString());
}

std::optional<IpcCommand> ipcCommandNamed(const QString &name)
{
    for (const auto &[command, commandName] : kCommandNames) {
        if (name == QLatin1String(commandName)) {
            return command;
        }
    }
    return std::nullopt;
}

QByteArray ipcRequestFrame(const IpcRequest &request)
{
    QJsonObject object{{QStringLiteral("command"), ipcCommandName(request.command)}};
    const SessionOverrides &overrides = request.overrides;
    if (overrides.outputFormat) {
        object.insert(QStringLiteral("outputFormat"), outputFormatName(*overrides.outputFormat));
    }
    if (overrides.writingProfile) {
        object.insert(QStringLiteral("writingProfile"), *overrides.writingProfile);
    }
    if (overrides.spokenLanguage) {
        object.insert(QStringLiteral("spokenLanguage"), *overrides.spokenLanguage);
    }
    if (!request.files.isEmpty()) {
        object.insert(QStringLiteral("files"), QJsonArray::fromStringList(request.files));
    }
    if (!request.terms.isEmpty()) {
        object.insert(QStringLiteral("terms"), QJsonArray::fromStringList(request.terms));
    }
    if (request.microphoneOnly) {
        object.insert(QStringLiteral("microphoneOnly"), true);
    }
    return frame(object);
}

std::optional<IpcRequest> ipcRequestFromFrame(const QByteArray &frame, IpcResponse *refusal)
{
    const auto refuse = [refusal](const QString &message) {
        *refusal = {false, {}, message};
        return std::nullopt;
    };
    const std::optional<QJsonObject> object = jsonObject(frame);
    if (!object) {
        return refuse(QStringLiteral("Invalid request"));
    }
    // An empty field is one the client left out: earlier releases read them
    // so.
    IpcRequest request;
    const QString outputFormat = object->value(QStringLiteral("outputFormat")).toString();
    if (!outputFormat.isEmpty()) {
        request.overrides.outputFormat = outputFormatFromString(outputFormat);
        if (!request.overrides.outputFormat) {
            return refuse(QStringLiteral("Unknown output format"));
        }
    }
    if (const QString profile = object->value(QStringLiteral("writingProfile")).toString(); !profile.isEmpty()) {
        request.overrides.writingProfile = profile;
    }
    if (const QString language = object->value(QStringLiteral("spokenLanguage")).toString(); !language.isEmpty()) {
        if (!isKnownSpokenLanguage(language)) {
            return refuse(QStringLiteral("Unknown spoken language"));
        }
        request.overrides.spokenLanguage = language;
    }
    const std::optional<QStringList> files = stringList(object->value(QStringLiteral("files")));
    const std::optional<QStringList> terms = stringList(object->value(QStringLiteral("terms")));
    if (!files || !terms) {
        return refuse(QStringLiteral("Invalid request"));
    }
    request.files = *files;
    request.terms = *terms;
    request.microphoneOnly = object->value(QStringLiteral("microphoneOnly")).toBool();
    const std::optional<IpcCommand> command = ipcCommandNamed(object->value(QStringLiteral("command")).toString());
    if (!command) {
        *refusal = {};
        refusal->unknownCommand = true;
        return std::nullopt;
    }
    request.command = *command;
    return request;
}

QByteArray ipcResponseFrame(const IpcResponse &response)
{
    const QString message = response.unknownCommand ? kUnknownCommandMessage : response.message;
    QJsonObject object{
        {QStringLiteral("ok"), response.ok},
        {QStringLiteral("state"), response.state},
        {QStringLiteral("message"), message.isEmpty() ? QJsonValue() : QJsonValue(message)},
    };
    if (!response.writingProfile.isEmpty()) {
        object.insert(QStringLiteral("writingProfile"), response.writingProfile);
    }
    if (!response.spokenLanguage.isEmpty()) {
        object.insert(QStringLiteral("spokenLanguage"), response.spokenLanguage);
    }
    if (!response.text.isEmpty()) {
        object.insert(QStringLiteral("text"), response.text);
    }
    if (!response.skippedTerms.isEmpty()) {
        object.insert(QStringLiteral("skippedTerms"), QJsonArray::fromStringList(response.skippedTerms));
    }
    if (response.recording) {
        object.insert(QStringLiteral("recording"), recordingStatusJson(*response.recording));
    }
    return frame(object);
}

std::optional<IpcResponse> ipcResponseFromFrame(const QByteArray &frame)
{
    const std::optional<QJsonObject> object = jsonObject(frame);
    if (!object) {
        return std::nullopt;
    }
    IpcResponse response;
    response.ok = object->value(QStringLiteral("ok")).toBool();
    response.state = object->value(QStringLiteral("state")).toString();
    response.message = object->value(QStringLiteral("message")).toString();
    response.unknownCommand = response.message == kUnknownCommandMessage;
    response.writingProfile = object->value(QStringLiteral("writingProfile")).toString();
    response.spokenLanguage = object->value(QStringLiteral("spokenLanguage")).toString();
    response.text = object->value(QStringLiteral("text")).toString();
    response.skippedTerms = stringList(object->value(QStringLiteral("skippedTerms"))).value_or(QStringList());
    if (object->contains(QStringLiteral("recording"))) {
        response.recording = recordingStatusFromJson(object->value(QStringLiteral("recording")).toObject());
    }
    return response;
}

} // namespace speecher
