#include "frontend/qt/MicrophoneTestRow.h"

#include "app/MicrophoneTest.h"
#include "dictation/DictationTypes.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>

namespace speecher {

namespace {

class MicrophoneTestControl final : public QWidget {
public:
    MicrophoneTestControl(ApplicationController &controller, QLabel *problem, QWidget *parent)
        : QWidget(parent)
        , m_test(new MicrophoneTest(controller, this))
    {
        auto *level = new QProgressBar(this);
        level->setRange(0, 100);
        level->setValue(0);
        level->setTextVisible(false);
        level->setAccessibleName(inputLevelLabel());
        auto *button = new QPushButton(this);
        button->setObjectName(QStringLiteral("microphoneTest"));

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(settings::relatedSpacing());
        layout->addWidget(level, 1);
        layout->addWidget(button);

        connect(button, &QPushButton::clicked, this, [this] { m_test->toggle(m_deviceId); });
        connect(m_test, &MicrophoneTest::levelChanged, level, [level](float value) {
            level->setValue(qBound(0, qRound(value * 100.0f), 100));
        });
        const auto follow = [this, level, button, problem] {
            button->setText(microphoneTestCaption(m_test->state()));
            button->setEnabled(m_test->canToggle());
            if (m_test->state() != MicrophoneTestState::Running) {
                level->setValue(0);
            }
            if (m_test->state() == MicrophoneTestState::Starting) {
                problem->hide();
            }
        };
        connect(m_test, &MicrophoneTest::changed, this, follow);
        follow();
        connect(m_test, &MicrophoneTest::failed, problem, [problem](const QString &message) {
            problem->setText(message);
            problem->show();
        });
    }

    // The Input device the page shows, saved or not yet.
    void followDevice(const QString &deviceId)
    {
        if (deviceId == m_deviceId) {
            return;
        }
        m_deviceId = deviceId;
        m_test->stop();
    }

protected:
    void hideEvent(QHideEvent *event) override
    {
        QWidget::hideEvent(event);
        m_test->stop();
    }

private:
    MicrophoneTest *m_test;
    QString m_deviceId;
};

} // namespace

SchemaCustomRowFactory microphoneTestRow(ApplicationController &controller)
{
    return [&controller](const SettingsRow &descriptor, QWidget *parent, std::function<void()>) {
        if (descriptor.id != QStringLiteral("microphoneTest")) {
            return SchemaCustomRow{};
        }
        auto *problem = new QLabel(parent);
        problem->setWordWrap(true);
        problem->setForegroundRole(QPalette::PlaceholderText);
        problem->setFont(settings::smallFont(problem->font()));
        problem->hide();
        auto *control = new MicrophoneTestControl(controller, problem, parent);
        SchemaCustomRow row{control, {}, {}};
        row.refresh = [control](const AppSettings &draft) { control->followDevice(draft.audio.deviceId); };
        row.detail = problem;
        return row;
    };
}

} // namespace speecher
