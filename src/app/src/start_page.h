// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QString>
#include <QStringList>
#include <QWidget>

#include <vector>

class QVBoxLayout;
class QGridLayout;

namespace proton::app {

// The page the app opens on (PLAN.md M4.11), in place of the sheet until the student picks
// something: New sheet and Open project, the recent files, and a card per example with its name
// and description. It only offers; MainWindow does what was chosen. Each control has an object
// name for the smoke checks: "start-new", "start-open", "start-recent-<i>", "start-example-<i>".
class StartPage : public QWidget {
    Q_OBJECT

public:
    struct Card
    {
        QString name;
        QString description;
    };

    explicit StartPage(QWidget* parent = nullptr);

    // The examples, in menu order, and the recent files, newest first.
    void setExamples(const std::vector<Card>& examples);
    void setRecentFiles(const QStringList& paths);

Q_SIGNALS:
    void newSheet();
    void openFile();
    void recentChosen(const QString& path);
    void exampleChosen(int index);

private:
    QVBoxLayout* recent_ = nullptr;
    QGridLayout* examples_ = nullptr;
};

} // namespace proton::app
