#include "ui/SettingsDialog.h"

#include "app/ApplicationController.h"
#include "ui/AccessibilityNotice.h"
#include "ui/settings/ApplicationSettingsPage.h"
#include "ui/settings/AudioSettingsPage.h"
#include "ui/settings/BindingsSettingsPage.h"
#include "ui/settings/CorrectionsSettingsPage.h"
#include "ui/settings/GeneralSettingsPage.h"
#include "ui/settings/OutputSettingsPage.h"
#include "ui/settings/ProviderSettingsPage.h"
#include "ui/settings/RefinementSettingsPage.h"
#include "ui/settings/SettingsPageSupport.h"
#include "ui/settings/SettingsPageSet.h"
#include "ui/settings/VocabularySettingsPage.h"

#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace speecher {

using namespace settings;

SettingsDialog::SettingsDialog(ApplicationController *controller, QWidget *parent)
    : QDialog(parent)
    , m_controller(controller)
{
    auto *scroll = new QScrollArea(this);
    setWindowTitle(QStringLiteral("Speecher Settings"));
    resize(980, 780);
    setMinimumSize(820, 620);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    m_accessibilityNotice = new AccessibilityNotice(this);
    root->addWidget(m_accessibilityNotice);
    m_pages = new SettingsPageSet(m_controller, this);

    auto *vocabularySection = makeSectionLabel(QStringLiteral("Vocabulary"), this);
    auto *correctionsSection = makeSectionLabel(QStringLiteral("Learned Corrections"), this);
    auto *bindingsSection = makeSectionLabel(QStringLiteral("Replacements & snippets"), this);

    m_pages->preserveBindingScroll(scroll);

    auto *vocabularyPageLayout = makeSettingsPage(scroll);
    vocabularyPageLayout->addWidget(vocabularySection);
    vocabularyPageLayout->addWidget(m_pages->vocabulary());
    vocabularyPageLayout->addWidget(correctionsSection);
    vocabularyPageLayout->addWidget(m_pages->corrections());
    vocabularyPageLayout->addWidget(bindingsSection);
    vocabularyPageLayout->addWidget(m_pages->bindings());
    vocabularyPageLayout->addStretch();

    const QList<QPair<QString, QString>> categories{
        {QStringLiteral("General"), QStringLiteral("preferences-system")},
        {QStringLiteral("Audio"), QStringLiteral("audio-input-microphone")},
        {QStringLiteral("Applications"), QStringLiteral("applications-system")},
        {QStringLiteral("Output"), QStringLiteral("edit-paste")},
        {QStringLiteral("Refinement"), QStringLiteral("document-edit")},
        {QStringLiteral("Providers"), QStringLiteral("network-server")},
        {QStringLiteral("Vocabulary"), QStringLiteral("tools-check-spelling")},
    };
    const QList<QWidget *> pages{
        m_pages->general(),
        m_pages->audio(),
        m_pages->applications(),
        m_pages->output(),
        m_pages->refinement(),
        m_pages->providers(),
        scroll,
    };

    auto *body = new QWidget(this);
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    QListWidget *categoriesWidget = nullptr;
    QStackedWidget *pagesWidget = nullptr;
    addPageContainer(bodyLayout,
                     categories,
                     pages,
                     &categoriesWidget,
                     &pagesWidget,
                     body);
    root->addWidget(body, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    buttons->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    auto *footer = new QFrame(this);
    footer->setFrameShape(QFrame::NoFrame);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(16, 12, 16, 12);
    auto *runtimeStatus = new QLabel(
        QStringLiteral("Dictation: %1").arg(m_controller->stateName()),
        footer);
    footerLayout->addWidget(runtimeStatus);
    footerLayout->addStretch();
    footerLayout->addWidget(buttons);
    root->addWidget(footer);

    applyLabelHierarchy(this);

    if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok)) {
        m_okButton = ok;
        ok->setDefault(true);
        ok->setAutoDefault(true);
        ok->setIcon(QIcon());
    }
    if (QPushButton *cancel = buttons->button(QDialogButtonBox::Cancel)) {
        cancel->setAutoDefault(false);
        cancel->setIcon(QIcon());
    }
    if (QPushButton *apply = buttons->button(QDialogButtonBox::Apply)) {
        m_applyButton = apply;
        apply->setAutoDefault(false);
        apply->setIcon(QIcon());
    }

    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (save()) {
            accept();
        }
    });
    connect(m_applyButton, &QPushButton::clicked, this, [this] {
        save();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_controller,
            &ApplicationController::statusChanged,
            runtimeStatus,
            [runtimeStatus](const QString &status) {
                runtimeStatus->setText(QStringLiteral("Dictation: %1").arg(status));
            });
    connect(m_pages, &SettingsPageSet::changed, this, &SettingsDialog::updateButtonState);
    connect(m_accessibilityNotice, &AccessibilityNotice::enableRequested, this, [this] {
        QString error;
        if (!m_controller->enableAccessibility(&error)) {
            m_accessibilityNotice->showError(error);
        }
    });
    connect(m_controller,
            &ApplicationController::accessibilityStateChanged,
            this,
            &SettingsDialog::updateAccessibilityState);
    updateAccessibilityState(m_controller->accessibilitySupported(),
                             m_controller->accessibilityEnabled(),
                             m_controller->accessibilityPersistent());
    updateButtonState();
}

bool SettingsDialog::save()
{
    if (!m_pages->save()) {
        return false;
    }
    updateButtonState();
    return true;
}

void SettingsDialog::updateButtonState()
{
    const bool changed = m_pages->hasChanges();
    if (m_okButton) {
        m_okButton->setEnabled(changed);
    }
    if (m_applyButton) {
        m_applyButton->setEnabled(changed);
    }
}

void SettingsDialog::updateAccessibilityState(bool supported,
                                              bool enabled,
                                              bool persistent)
{
    m_accessibilityNotice->setState(supported, enabled, persistent);
}

} // namespace speecher
