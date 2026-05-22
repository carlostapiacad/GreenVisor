#include "gui/StartScreenDialog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpacerItem>
#include <QVBoxLayout>

StartScreenDialog::StartScreenDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName("startScreen");
    setWindowTitle("GreenVisor");
    setModal(true);
    resize(960, 540);
    SetupBackground();

    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(32, 32, 32, 32);
    rootLayout->setSpacing(0);

    QWidget *panel = new QWidget(this);
    panel->setFixedWidth(420);
    panel->setStyleSheet(
        "background-color: rgba(255, 255, 255, 230);"
        "border: 1px solid #cfd7e3;"
        "border-radius: 6px;");

    QVBoxLayout *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(20, 20, 20, 20);
    panelLayout->setSpacing(12);

    QLabel *title = new QLabel("GreenVisor", panel);
    QFont titleFont("DengXian", 28, QFont::Bold);
    title->setFont(titleFont);
    title->setStyleSheet("color: #0f172a; background: transparent; border: none;");

    QLabel *subtitle = new QLabel("By Carlos Tapia", panel);
    QFont subtitleFont("DengXian", 11, QFont::Normal);
    subtitle->setFont(subtitleFont);
    subtitle->setStyleSheet("color: #475569; background: transparent; border: none;");

    panelLayout->addWidget(title);
    panelLayout->addWidget(subtitle);
    panelLayout->addSpacing(16);

    QLabel *openLabel = new QLabel("Open Project", panel);
    openLabel->setStyleSheet("color: #1f2933; background: transparent; border: none;");
    panelLayout->addWidget(openLabel);

    QHBoxLayout *openLayout = new QHBoxLayout();
    m_openPathEdit = new QLineEdit(panel);
    m_openPathEdit->setPlaceholderText("Select a project file (*.gvs)");
    m_openPathEdit->setMinimumHeight(30);
    m_openPathEdit->setStyleSheet(
        "QLineEdit {"
        " color: #1f2933;"
        " background: #ffffff;"
        " border: 1px solid #cfd7e3;"
        " border-radius: 0px;"
        " padding: 4px 6px; }"
        "QLineEdit::placeholder { color: #94a3b8; }");
    QPushButton *browseButton = new QPushButton("Browse", panel);
    browseButton->setFixedWidth(90);
    browseButton->setStyleSheet(
        "QPushButton { background: #ffffff; color: #1f2933; border: 1px solid #cfd7e3; border-radius: 2px; padding: 4px 10px; }"
        "QPushButton:hover { background: #f1f5f9; }");
    connect(browseButton, &QPushButton::clicked, this, &StartScreenDialog::OnBrowseOpen);
    openLayout->addWidget(m_openPathEdit, 1);
    openLayout->addWidget(browseButton);
    panelLayout->addLayout(openLayout);

    QHBoxLayout *actionRow = new QHBoxLayout();
    QPushButton *openButton = new QPushButton("Open", panel);
    QPushButton *newButton = new QPushButton("New Project", panel);
    openButton->setFixedWidth(110);
    newButton->setFixedWidth(140);
    const char *buttonStyle =
        "QPushButton { background: #ffffff; color: #1f2933; border: 1px solid #cfd7e3; border-radius: 2px; padding: 6px 12px; }"
        "QPushButton:hover { background: #f1f5f9; }";
    openButton->setStyleSheet(buttonStyle);
    newButton->setStyleSheet(buttonStyle);
    connect(openButton, &QPushButton::clicked, this, &StartScreenDialog::OnOpenProject);
    connect(newButton, &QPushButton::clicked, this, &StartScreenDialog::OnNewProject);
    actionRow->addWidget(openButton);
    actionRow->addWidget(newButton);
    actionRow->addStretch(1);
    panelLayout->addLayout(actionRow);

    panelLayout->addStretch(1);

    rootLayout->addWidget(panel, 0, Qt::AlignLeft | Qt::AlignTop);
    rootLayout->addStretch(1);
}

void StartScreenDialog::SetupBackground()
{
    const QString base = QCoreApplication::applicationDirPath();
    QString bgPath = QDir(base).filePath("../assets/Backgrounds/StartingScreen.png");
    if (!QFileInfo::exists(bgPath)) {
        bgPath = QDir(base).filePath("../assets/backgrounds/StartingScreen.png");
    }
    if (!QFileInfo::exists(bgPath)) {
        bgPath = QDir("assets/Backgrounds").filePath("StartingScreen.png");
    }
    if (!QFileInfo::exists(bgPath)) {
        bgPath = QDir("assets/backgrounds").filePath("StartingScreen.png");
    }

    if (QFileInfo::exists(bgPath)) {
        setStyleSheet(QString(
            "QDialog#startScreen {"
            " border-image: url(\"%1\") 0 0 0 0 stretch stretch;"
            "}").arg(bgPath));
    } else {
        setStyleSheet("QDialog#startScreen { background-color: #f8fafc; }");
    }
}

void StartScreenDialog::OnBrowseOpen()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        "Open Project",
        QString(),
        "GreenVisor Project (*.gvs)");
    if (!path.isEmpty() && m_openPathEdit) {
        m_openPathEdit->setText(path);
    }
}

void StartScreenDialog::OnOpenProject()
{
    if (!m_openPathEdit) {
        return;
    }
    QString path = m_openPathEdit->text().trimmed();
    if (path.isEmpty()) {
        OnBrowseOpen();
        path = m_openPathEdit->text().trimmed();
    }
    if (path.isEmpty()) {
        return;
    }
    m_projectPath = EnsureProjectExtension(path);
    m_action = Action::Open;
    accept();
}

void StartScreenDialog::OnNewProject()
{
    const QString path = QFileDialog::getSaveFileName(
        this,
        "Create Project",
        QString(),
        "GreenVisor Project (*.gvs)");
    if (path.isEmpty()) {
        return;
    }
    m_projectPath = EnsureProjectExtension(path);
    m_action = Action::New;
    accept();
}

QString StartScreenDialog::EnsureProjectExtension(const QString &path)
{
    if (path.endsWith(".gvs", Qt::CaseInsensitive)) {
        return path;
    }
    return path + ".gvs";
}
