#pragma once

#include <QGroupBox>
#include <QList>
#include <QString>
#include <QWidget>

class QBoxLayout;
class QLabel;
class QVBoxLayout;

namespace speecher::settings {

// One row of a settings card: a title and an optional one-line subtitle on the
// left, the control on the right, vertically centred. An editor too wide for
// the control column (a table, a key-sequence editor) spans the row below the
// title instead.
class FormRow : public QWidget {
    Q_OBJECT

public:
    FormRow(const QString &title, const QString &subtitle, QWidget *parent = nullptr);

    void setControl(QWidget *control);
    void setEditor(QWidget *editor);
    // Sits under the subtitle in the text column, such as a sign-in status.
    void setDetail(QWidget *detail);
    void setSubtitle(const QString &text);

    QString title() const;
    QString subtitle() const;
    QWidget *control() const { return m_control; }
    QWidget *editor() const { return m_editor; }
    QLabel *titleLabel() const { return m_title; }
    QLabel *subtitleLabel() const { return m_subtitle; }

    // What a search can match: the title, the subtitle and the words on the
    // control.
    QString searchText() const;

    // A brief selection highlight, the way a found item is pointed at.
    void flash();
    bool isFlashing() const { return m_flashing; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void applyFlashPalette(bool flashing);

    QLabel *m_title;
    QLabel *m_subtitle;
    QWidget *m_textColumn;
    QVBoxLayout *m_textLayout;
    QBoxLayout *m_headLayout;
    QVBoxLayout *m_layout;
    QWidget *m_control = nullptr;
    QWidget *m_editor = nullptr;
    QWidget *m_detail = nullptr;
    bool m_flashing = false;
};

// One card of a settings page: a header above it (title, optional one-line
// description) and the rows inside, separated by the style's own hairlines.
// The card body is a title-less QGroupBox, so its shape and colours are the
// style's.
class SettingsCard : public QWidget {
    Q_OBJECT

public:
    SettingsCard(const QString &title, const QString &description, QWidget *parent = nullptr);

    QString title() const;
    QLabel *titleLabel() const { return m_title; }
    QLabel *descriptionLabel() const { return m_description; }
    QGroupBox *body() const { return m_body; }

    // Appends a row. A separator goes before every row but the first; whether
    // it shows follows the rows around it, see updateSeparators.
    void addRow(QWidget *row);
    QList<QWidget *> rows() const { return m_rows; }
    // Shows a separator only between two rows that are both shown.
    void updateSeparators();

private:
    QLabel *m_title;
    QLabel *m_description;
    QGroupBox *m_body;
    QVBoxLayout *m_bodyLayout;
    QList<QWidget *> m_rows;
    QList<QWidget *> m_separators;
};

// The column a settings page stacks its cards in: centred in the page, at most
// cardMaximumWidth() wide, with sectionGap() between cards.
QVBoxLayout *makeCardColumn(QBoxLayout *pageLayout, QWidget *parent);

} // namespace speecher::settings
