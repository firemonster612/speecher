#pragma once

#include <QDialog>

class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;

namespace speecher {

class ApplicationController;

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(ApplicationController *controller, QWidget *parent = nullptr);

private:
    void load();
    void save();
    bool hasChanges() const;
    QStringList currentVocabulary() const;
    void setVocabularyRows(const QStringList &terms);
    void updateAuthControl();
    void updateButtonState();
    void updateVocabularyLimit();

    ApplicationController *m_controller = nullptr;
    QComboBox *m_theme = nullptr;
    QCheckBox *m_pauseMedia = nullptr;
    QComboBox *m_provider = nullptr;
    QComboBox *m_refinementStyle = nullptr;
    QComboBox *m_outputFormat = nullptr;
    QComboBox *m_authMode = nullptr;
    QStackedWidget *m_authControl = nullptr;
    QLabel *m_authStatus = nullptr;
    QLabel *m_vocabLimit = nullptr;
    QLineEdit *m_apiKey = nullptr;
    QPushButton *m_okButton = nullptr;
    QPushButton *m_applyButton = nullptr;
    QSpinBox *m_previewWords = nullptr;
    QTableWidget *m_vocab = nullptr;
    bool m_updatingVocabulary = false;
};

} // namespace speecher
