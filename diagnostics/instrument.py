from pathlib import Path

def instrument(file, entries):
    p=Path(file)
    s=p.read_text()
    s='#include <QDebug>\n#include <QElapsedTimer>\n#include <QScopeGuard>\n'+s
    for signature,label in entries:
        anchor=signature+'\n{'
        if anchor not in s: raise RuntimeError(anchor)
        s=s.replace(anchor,anchor+f'\n    QElapsedTimer debugTimer; debugTimer.start();\n    const auto debugTiming = qScopeGuard([&] {{ qInfo() << "[DEBUG-win-slow-2] {label}" << debugTimer.nsecsElapsed(); }});',1)
    p.write_text(s)

instrument('src/core/SettingsStore.cpp', [('AppSettings SettingsStore::snapshot() const','snapshot'),('AppSettings SettingsStore::dictationSnapshot() const','dictation_snapshot')])
instrument('src/app/LocalSetup.cpp', [('LiveFacts LocalSetup::liveFacts() const','livefacts_saved'),('LiveFacts LocalSetup::liveFacts(const AppSettings &draft) const','livefacts_draft'),('void LocalSetup::probeHardware()','hardware_request')])
instrument('src/core/CliToolDiscovery.cpp',[('QString CliToolDiscovery::codexExecutable()','cli_codex'),('QString CliToolDiscovery::claudeCodeExecutable()','cli_claude')])
instrument('src/ui/settings/SettingsPageSet.cpp', [('void SettingsPageSet::loadBeforeShow()','pages_before'),('void SettingsPageSet::loadAfterShow()','pages_after')])
p=Path('src/ui/settings/SettingsPageSet.cpp');s=p.read_text();anchor='    const SchemaCustomRowFactory customRows = combinedRows({'
s=s.replace(anchor,'    QElapsedTimer debugCtorTimer; debugCtorTimer.start();\n    const auto debugCtorTiming = qScopeGuard([&] { qInfo() << "[DEBUG-win-slow-2] pages_ctor" << debugCtorTimer.nsecsElapsed(); });\n'+anchor)
p.write_text(s)
p=Path('src/app/LocalSetup.cpp');s=p.read_text();s=s.replace('if (m_models.isDownloaded(model)) {','if (!qEnvironmentVariableIsSet("DEBUG_SKIP_MODEL_SCAN") && m_models.isDownloaded(model)) {');s=s.replace('    if (m_hardwareKnown || m_hardwareProbing) {','    if (qEnvironmentVariableIsSet("DEBUG_SKIP_HARDWARE")) return;\n    if (m_hardwareKnown || m_hardwareProbing) {');p.write_text(s)
