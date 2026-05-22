#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;

class StartScreenDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Action
    {
        None,
        Open,
        New
    };

    explicit StartScreenDialog(QWidget *parent = nullptr);

    Action action() const { return m_action; }
    QString projectPath() const { return m_projectPath; }

private:
    void SetupBackground();
    void OnBrowseOpen();
    void OnOpenProject();
    void OnNewProject();
    static QString EnsureProjectExtension(const QString &path);

    Action m_action = Action::None;
    QString m_projectPath;
    QLineEdit *m_openPathEdit = nullptr;
};
