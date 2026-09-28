#include "frontend/qt/TrayStatusPanel.h"

#include "app/ApplicationController.h"
#include "dictation/DictationTypes.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QClipboard>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QStyle>
#include <QTextLayout>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>

#ifdef SPEECHER_WITH_LAYER_SHELL
#include <LayerShellQt/Window>
#endif

namespace speecher {
namespace {

// The macOS popover and the Windows flyout show three lines of it too.
constexpr int kTranscriptLines = 3;

// text as it fits in lines lines of width, the last one elided.
QString firstLines(const QString &text, const QFont &font, int width, int lines)
{
    QTextLayout layout(text, font);
    layout.beginLayout();
    int lastStart = 0;
    for (int line = 0; line < lines; ++line) {
        QTextLine next = layout.createLine();
        if (!next.isValid()) {
            layout.endLayout();
            return text;
        }
        next.setLineWidth(width);
        lastStart = next.textStart();
    }
    const bool more = layout.createLine().isValid();
    layout.endLayout();
    if (!more) {
        return text;
    }
    return text.left(lastStart)
        + QFontMetrics(font).elidedText(text.mid(lastStart).simplified(), Qt::ElideRight, width);
}

QIcon statusIcon(bool listening)
{
    return QIcon::fromTheme(listening ? QStringLiteral("audio-input-microphone")
                                      : QStringLiteral("io.github.firemonster612.speecher"),
                            QIcon::fromTheme(QStringLiteral("audio-input-microphone")));
}

QPushButton *panelButton(const QString &caption, QWidget *parent)
{
    auto *button = new QPushButton(caption, parent);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return button;
}

bool onWayland()
{
    return QGuiApplication::platformName().startsWith(QStringLiteral("wayland"));
}

} // namespace

TrayStatusPanel::TrayStatusPanel(ApplicationController *controller, QWidget *parent)
    : QFrame(parent)
    , m_controller(controller)
{
    setObjectName(QStringLiteral("trayStatusPanel"));
    // The style draws the popup's frame, as it does a menu's.
    setFrameShape(QFrame::StyledPanel);
    // A Wayland client cannot place a popup it has no parent for, so there it
    // is a layer-shell surface; on X11 a real popup, which closes itself on a
    // click elsewhere.
    setWindowFlags(onWayland() ? Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool
                               : Qt::Popup);
    setFixedWidth(settings::gridUnit() * 17);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(settings::rowPadding());
    layout->setSpacing(settings::largeSpacing());

    auto *heading = new QHBoxLayout;
    heading->setSpacing(settings::relatedSpacing());
    m_icon = new QLabel(this);
    m_heading = new QLabel(this);
    m_heading->setObjectName(QStringLiteral("trayPanelHeading"));
    m_heading->setWordWrap(true);
    QFont bold = m_heading->font();
    bold.setBold(true);
    m_heading->setFont(bold);
    heading->addWidget(m_icon, 0, Qt::AlignTop);
    heading->addWidget(m_heading, 1);
    layout->addLayout(heading);

    m_level = new QProgressBar(this);
    m_level->setObjectName(QStringLiteral("trayPanelLevel"));
    m_level->setRange(0, 100);
    m_level->setTextVisible(false);
    m_level->setAccessibleName(QStringLiteral("Input level"));
    layout->addWidget(m_level);

    m_toggle = panelButton(QString(), this);
    m_toggle->setObjectName(QStringLiteral("trayPanelToggle"));
    m_toggle->setDefault(true);
    layout->addWidget(m_toggle);

    layout->addWidget(settings::makeSeparator(this));
    m_transcript = new QLabel(this);
    m_transcript->setObjectName(QStringLiteral("trayPanelTranscript"));
    m_transcript->setWordWrap(true);
    m_transcript->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(m_transcript);
    m_copy = panelButton(copyTranscriptCaption(), this);
    m_copy->setObjectName(QStringLiteral("trayPanelCopy"));
    m_copy->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
    layout->addWidget(m_copy);

    layout->addWidget(settings::makeSeparator(this));
    QPushButton *settingsButton = panelButton(traySettingsCaption(), this);
    settingsButton->setObjectName(QStringLiteral("trayPanelSettings"));
    layout->addWidget(settingsButton);
    QPushButton *quit = panelButton(trayQuitCaption(), this);
    quit->setObjectName(QStringLiteral("trayPanelQuit"));
    layout->addWidget(quit);

    connect(m_toggle, &QPushButton::clicked, this, [this] {
        hide();
        m_controller->toggle();
    });
    connect(m_copy, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(m_controller->lastTranscript());
    });
    connect(settingsButton, &QPushButton::clicked, this, [this] {
        hide();
        m_controller->showSettingsWindow();
    });
    connect(quit, &QPushButton::clicked, this, [this] {
        hide();
        m_controller->quitApplication();
    });
    connect(controller, &ApplicationController::stateChanged, this, &TrayStatusPanel::applyState);
    connect(controller, &ApplicationController::statusChanged, m_heading, &QLabel::setText);
    connect(controller, &ApplicationController::audioLevelChanged, this, [this](float level) {
        m_level->setValue(qRound(std::clamp(level, 0.0f, 1.0f) * 100));
    });
    connect(controller, &ApplicationController::lastTranscriptChanged, this,
            &TrayStatusPanel::showTranscript);

    applyState(controller->stateName());
    m_heading->setText(controller->statusLabel());
    showTranscript(controller->lastTranscript());
}

void TrayStatusPanel::applyState(const QString &stateName)
{
    const bool listening = dictationListeningPresentation(stateName);
    const int extent = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
    m_icon->setPixmap(statusIcon(listening).pixmap(extent, extent));
    m_level->setVisible(listening);
    if (!listening) {
        m_level->setValue(0);
    }
    const DictationToggleAction toggle = dictationToggleAction(stateName);
    m_toggle->setText(toggle.label);
    m_toggle->setEnabled(toggle.enabled);
    fitHeight();
}

void TrayStatusPanel::showTranscript(const QString &text)
{
    const QString shown = text.simplified();
    const int width = std::max(1, contentsRect().width() - layout()->contentsMargins().left()
                                      - layout()->contentsMargins().right());
    m_transcript->setText(shown.isEmpty() ? noTranscriptYetText()
                                          : firstLines(shown, m_transcript->font(), width,
                                                       kTranscriptLines));
    m_copy->setVisible(!shown.isEmpty());
    fitHeight();
}

void TrayStatusPanel::fitHeight()
{
    // adjustSize() measures a window at its size hint's width, which is wider
    // than the fixed width the wrapped heading and transcript are laid out at.
    resize(width(), heightForWidth(width()));
}

void TrayStatusPanel::popUp(const QRect &anchor)
{
    showTranscript(m_controller->lastTranscript());
    QScreen *screen = QGuiApplication::screenAt(anchor.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (onWayland()) {
#ifdef SPEECHER_WITH_LAYER_SHELL
        winId();
        if (auto *layer = LayerShellQt::Window::get(windowHandle())) {
            layer->setScope(QStringLiteral("speecher-tray"));
            layer->setLayer(LayerShellQt::Window::LayerTop);
            layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityOnDemand);
            layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorBottom
                                                            | LayerShellQt::Window::AnchorRight));
            // With no exclusive zone of its own the surface is placed inside
            // what the panels leave free, so this corner is beside the tray
            // of Plasma's default bottom panel.
            const int gap = settings::smallSpacing();
            layer->setMargins(QMargins(0, 0, gap, gap));
#ifdef SPEECHER_LAYER_SHELL_HAS_WINDOW_SCREEN
            layer->setScreen(screen);
#endif
        }
#endif
    } else {
        // Beside the icon, on the side of it the screen has room for: above a
        // bottom panel, below a top one.
        const QRect area = screen->availableGeometry();
        const QSize size = this->size();
        const int gap = settings::smallSpacing();
        const int x = std::clamp(anchor.center().x() - size.width() / 2, area.left(),
                                 area.right() - size.width() + 1);
        const int y = anchor.center().y() > area.center().y() ? anchor.top() - size.height() - gap
                                                              : anchor.bottom() + gap;
        move(x, std::clamp(y, area.top(), area.bottom() - size.height() + 1));
    }
    show();
    raise();
    activateWindow();
}

bool TrayStatusPanel::event(QEvent *event)
{
    // A layer-shell surface or a plain window does not close itself when
    // something else is clicked, as a popup does; losing focus stands in.
    if (event->type() == QEvent::WindowDeactivate && !windowFlags().testFlag(Qt::Popup)) {
        hide();
    } else if (event->type() == QEvent::KeyPress
               && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        hide();
        return true;
    }
    return QFrame::event(event);
}

} // namespace speecher
