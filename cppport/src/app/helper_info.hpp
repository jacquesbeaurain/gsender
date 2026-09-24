#pragma once

// The Helper's info panel (features/Helper/HelperInfo, opened through
// pubsub's 'helper:info'): a card over the top left of the main window - an
// info mark, the title, the explanation, a link to the resources - that
// stays, blocking nothing, until closed or replaced. Alarm explanations and
// the bad file and bad line warnings open it.

#include <QFrame>

class QLabel;

namespace gs::app {

class HelperInfo final : public QFrame {
    Q_OBJECT
public:
    // Floats over `parent`, following its size.
    explicit HelperInfo(QWidget* parent);

    // `description` is rich text; a `resourceLink` adds "Learn more".
    void showInfo(const QString& title, const QString& description, const QString& resourceLink = {});
    QString title() const;
    QString description() const;  // as plain text
    QString resourceLink() const { return link_; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void place();

    QLabel* title_;
    QLabel* description_;
    QLabel* more_;
    QString link_;
};

}  // namespace gs::app
