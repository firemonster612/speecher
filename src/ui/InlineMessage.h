#pragma once

#include <QFrame>

class QHBoxLayout;
class QLabel;
class QToolButton;

namespace speecher {

// Kirigami's InlineMessage in Qt Widgets: a tinted, bordered strip with a
// type icon, wrapping text, optional actions on the right and a close button.
// Inline messages are rounded and bordered all round; a Header message sits
// flush at the top of a pane with only a bottom hairline.
class InlineMessage : public QFrame
{
    Q_OBJECT
public:
    enum class Type { Information, Positive, Warning, Error };
    enum class Position { Inline, Header };

    explicit InlineMessage(QWidget *parent = nullptr);

    void setType(Type type);
    Type type() const { return m_type; }
    void setPosition(Position position);
    void setText(const QString &text);
    QLabel *label() const { return m_label; }
    // Placed to the right of the text, before the close button.
    void addAction(QWidget *action);
    QToolButton *closeButton() const { return m_close; }
    void setCloseButtonVisible(bool visible);

    // The colour Kirigami uses for this message type: the border, the tint
    // and the icon all derive from it.
    QColor typeColor() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void refreshIcon();

    Type m_type = Type::Information;
    Position m_position = Position::Inline;
    QLabel *m_icon = nullptr;
    QLabel *m_label = nullptr;
    QHBoxLayout *m_actions = nullptr;
    QToolButton *m_close = nullptr;
};

} // namespace speecher
