#pragma once

#include "test_suites.h"

#include "app/ApplicationController.h"
#include "app/SingleInstanceIpc.h"
#include "core/AppSettings.h"
#include "core/BindingProcessor.h"
#include "core/OutputMethod.h"
#include "core/OutputFormat.h"
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "core/TranscriptState.h"
#include "core/VocabularyLimit.h"
#include "core/Vocabulary.h"
#include "core/WordPreview.h"
#include "dictation/DictationSession.h"
#include "dictation/DictationTypes.h"
#include "providers/AnthropicApiRefiner.h"
#include "providers/ClaudeCredentials.h"
#include "providers/ClaudeVoiceClient.h"
#include "providers/OpenAiAuthProvider.h"
#include "providers/OpenAiRefiner.h"
#include "providers/ProviderRegistry.h"
#include "providers/TranscriptRefinementPrompt.h"
#include "output/TextDelivery.h"
#include "output/WlClipboardDelivery.h"
#include "output/YdotoolDelivery.h"
#include "output/YdotoolSetup.h"
#include "platform/PlatformIntegration.h"
#include "platform/AtSpiTargetProvider.h"
#include "platform/PortalScreenshotContextProvider.h"
#include "platform/audio/AudioPcmConverter.h"
#include "platform/audio/QtAudioInput.h"
#include "ui/Theme.h"
#include "ui/SettingsDialog.h"
#include "ui/TranscriberPopup.h"
#include "ui/WaveformWidget.h"

#include <QBoxLayout>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHostAddress>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMimeData>
#include <QMouseEvent>
#include <QPalette>
#include <QProgressBar>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#ifdef SPEECHER_WITH_QT_WEBSOCKETS
#include <QWebSocket>
#include <QWebSocketServer>
#endif
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QUuid>
#include <QtTest>

#ifdef SPEECHER_WITH_KPAGEWIDGET
#include <KPageWidget>
#endif

#include <algorithm>
#include <future>
#include <utility>

using namespace speecher;
