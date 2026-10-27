// SPDX-License-Identifier: AGPL-3.0-only
#include "start_page.h"

#include <QEnterEvent>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLayoutItem>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <functional>

namespace proton::app {

namespace {

constexpr int kColumns = 2; // example cards per row

// A clickable card: an example's name above its description.
class CardButton : public QFrame
{
public:
    CardButton(const StartPage::Card& card, QWidget* parent) : QFrame(parent)
    {
        setFrameShape(QFrame::StyledPanel);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setBackgroundRole(QPalette::Base);
        setAutoFillBackground(true);
        setAccessibleName(card.name);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(14, 12, 14, 12);
        auto* name = new QLabel(card.name, this);
        QFont bold = name->font();
        bold.setBold(true);
        bold.setPointSizeF(bold.pointSizeF() * 1.15);
        name->setFont(bold);
        auto* description = new QLabel(card.description, this);
        description->setWordWrap(true);
        description->setForegroundRole(QPalette::PlaceholderText);
        layout->addWidget(name);
        layout->addWidget(description);
        layout->addStretch(1);
        setMinimumWidth(260);
    }

    std::function<void()> clicked;

protected:
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) &&
            clicked) {
            clicked();
        }
    }
    void keyPressEvent(QKeyEvent* event) override
    {
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
             event->key() == Qt::Key_Space) &&
            clicked) {
            clicked();
            return;
        }
        QFrame::keyPressEvent(event);
    }
    void enterEvent(QEnterEvent* /*event*/) override { setBackgroundRole(QPalette::AlternateBase); }
    void leaveEvent(QEvent* /*event*/) override { setBackgroundRole(QPalette::Base); }
};

QLabel* heading(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 1.2);
    label->setFont(font);
    return label;
}

void clear(QLayout* layout)
{
    while (QLayoutItem* item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}

} // namespace

StartPage::StartPage(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("start-page"));
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll);
    scroll->setWidget(content);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    auto* page = new QHBoxLayout(content);
    page->setContentsMargins(48, 40, 48, 40);
    page->setSpacing(48);

    // Left: the title, then what to start with, then the recent files.
    auto* left = new QVBoxLayout;
    left->setSpacing(10);
    auto* title = new QLabel(QStringLiteral("Proton"), content);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * 2.4);
    title->setFont(titleFont);
    left->addWidget(title);
    auto* subtitle = new QLabel(tr("A bench of instruments for your circuits."), content);
    subtitle->setForegroundRole(QPalette::PlaceholderText);
    left->addWidget(subtitle);
    left->addSpacing(20);
    left->addWidget(heading(tr("Start"), content));
    auto* newSheet = new QPushButton(tr("New sheet"), content);
    newSheet->setObjectName(QStringLiteral("start-new"));
    connect(newSheet, &QPushButton::clicked, this, &StartPage::newSheet);
    auto* open = new QPushButton(tr("Open project..."), content);
    open->setObjectName(QStringLiteral("start-open"));
    connect(open, &QPushButton::clicked, this, &StartPage::openFile);
    left->addWidget(newSheet);
    left->addWidget(open);
    left->addSpacing(20);
    left->addWidget(heading(tr("Recent"), content));
    recent_ = new QVBoxLayout;
    recent_->setSpacing(4);
    left->addLayout(recent_);
    left->addStretch(1);
    page->addLayout(left, 1);

    // Right: the examples.
    auto* right = new QVBoxLayout;
    right->setSpacing(10);
    right->addWidget(heading(tr("Examples"), content));
    examples_ = new QGridLayout;
    examples_->setSpacing(12);
    right->addLayout(examples_);
    right->addStretch(1);
    page->addLayout(right, 2);

    setRecentFiles({});
}

void StartPage::setExamples(const std::vector<Card>& examples)
{
    clear(examples_);
    for (std::size_t i = 0; i < examples.size(); ++i) {
        auto* card = new CardButton(examples[i], this);
        card->setObjectName(QStringLiteral("start-example-%1").arg(i));
        const int index = static_cast<int>(i);
        card->clicked = [this, index] { Q_EMIT exampleChosen(index); };
        examples_->addWidget(card, index / kColumns, index % kColumns);
    }
    if (examples.empty()) {
        examples_->addWidget(new QLabel(tr("No examples were found next to Proton."), this), 0, 0);
    }
}

void StartPage::setRecentFiles(const QStringList& paths)
{
    clear(recent_);
    for (qsizetype i = 0; i < paths.size(); ++i) {
        const QString path = paths[i];
        auto* button = new QPushButton(QFileInfo(path).completeBaseName(), this);
        button->setObjectName(QStringLiteral("start-recent-%1").arg(i));
        button->setToolTip(path);
        button->setFlat(true);
        button->setStyleSheet(QStringLiteral("text-align: left;"));
        connect(button, &QPushButton::clicked, this, [this, path] { Q_EMIT recentChosen(path); });
        recent_->addWidget(button);
    }
    if (paths.isEmpty()) {
        auto* none = new QLabel(tr("Projects you save or open appear here."), this);
        none->setForegroundRole(QPalette::PlaceholderText);
        recent_->addWidget(none);
    }
}

} // namespace proton::app
