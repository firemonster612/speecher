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
        auto *button = new QPushButton(microphoneTestCaption(false), this);
        button->setObjectName(QStringLiteral("microphoneTest"));

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(settings::relatedSpacing());
        layout->addWidget(level, 1);
        layout->addWidget(button);

        connect(button, &QPushButton::clicked, this, [this] {
            if (m_test->running()) {
                m_test->stop();
            } else {
                m_test->start();
            }
        });
        connect(m_test, &MicrophoneTest::levelChanged, level, [level](float value) {
            level->setValue(qBound(0, qRound(value * 100.0f), 100));
        });
        connect(m_test, &MicrophoneTest::runningChanged, this, [level, button, problem](bool running) {
            button->setText(microphoneTestCaption(running));
            level->setValue(0);
            problem->hide();
        });
        connect(m_test, &MicrophoneTest::failed, problem, [problem](const QString &message) {
            problem->setText(message);
            problem->show();
        });
    }

protected:
    void hideEvent(QHideEvent *event) override
    {
        QWidget::hideEvent(event);
        m_test->stop();
    }

private:
    MicrophoneTest *m_test;
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
        SchemaCustomRow row{new MicrophoneTestControl(controller, problem, parent), {}, {}};
        row.detail = problem;
        return row;
    };
}

} // namespace speecher
