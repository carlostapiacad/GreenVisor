#include "gui/StartScreenDialog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace
{
QString FindAssetPath(const QString &relativePath)
{
    const QString base = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(base).filePath("../" + relativePath),
        QDir(base).filePath("../../" + relativePath),
        QDir::current().filePath(relativePath),
        relativePath};
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }
    return {};
}
}

StartScreenDialog::StartScreenDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName("startScreen");
    setWindowTitle("GreenVisor");
    setModal(true);
    resize(1120, 760);
    setMinimumSize(920, 620);
    SetupBackground();

    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(42, 38, 42, 34);
    rootLayout->setSpacing(0);

    const QString logoPath = FindAssetPath("assets/Icons/Logo.png");
    if (!logoPath.isEmpty()) {
        setWindowIcon(QIcon(logoPath));
    }

    QHBoxLayout *brandRow = new QHBoxLayout();
    brandRow->setContentsMargins(0, 0, 0, 0);
    brandRow->setSpacing(22);

    QLabel *logo = new QLabel(this);
    logo->setObjectName("startupLogo");
    logo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    if (!logoPath.isEmpty()) {
        QPixmap logoPixmap(logoPath);
        logo->setPixmap(logoPixmap.scaled(QSize(106, 106), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        logo->setFixedSize(86, 86);
    }
    brandRow->addWidget(logo, 0, Qt::AlignVCenter);

    QWidget *brandDivider = new QWidget(this);
    brandDivider->setObjectName("startupBrandDivider");
    brandDivider->setFixedWidth(1);
    brandDivider->setMinimumHeight(70);
    brandRow->addWidget(brandDivider, 0, Qt::AlignVCenter);

    QVBoxLayout *brandText = new QVBoxLayout();
    brandText->setContentsMargins(0, 0, 0, 0);
    brandText->setSpacing(6);
    QLabel *brandName = new QLabel("GreenVisor", this);
    brandName->setObjectName("startupBrandName");
    QFont brandFont("Segoe UI", 34, QFont::Bold);
    brandName->setFont(brandFont);
    QLabel *brandSubtitle = new QLabel("Apiri OpenSource", this);
    brandSubtitle->setObjectName("startupBrandSubtitle");
    QFont brandSubtitleFont("Segoe UI", 14, QFont::Normal);
    brandSubtitle->setFont(brandSubtitleFont);
    brandText->addWidget(brandName);
    brandText->addWidget(brandSubtitle);
    brandRow->addLayout(brandText);
    brandRow->addStretch(1);
    rootLayout->addLayout(brandRow);
    rootLayout->addStretch(1);

    QWidget *startupPanel = new QWidget(this);
    startupPanel->setObjectName("startupPanel");
    QVBoxLayout *startupLayout = new QVBoxLayout(startupPanel);
    startupLayout->setContentsMargins(28, 24, 28, 24);
    startupLayout->setSpacing(12);

    QVBoxLayout *headingText = new QVBoxLayout();
    headingText->setContentsMargins(0, 0, 0, 0);
    headingText->setSpacing(7);
    QLabel *title = new QLabel("Project Startup", startupPanel);
    title->setObjectName("startupTitle");
    QFont titleFont("Segoe UI", 22, QFont::DemiBold);
    title->setFont(titleFont);
    QLabel *subtitle = new QLabel("Open an existing project or create a new one to get started.", startupPanel);
    subtitle->setObjectName("startupSubtitle");
    QFont subtitleFont("Segoe UI", 12, QFont::Normal);
    subtitle->setFont(subtitleFont);
    headingText->addWidget(title);
    headingText->addWidget(subtitle);
    startupLayout->addLayout(headingText);
    startupLayout->addSpacing(34);

    QHBoxLayout *openLayout = new QHBoxLayout();
    openLayout->setSpacing(18);
    m_openPathEdit = new QLineEdit(startupPanel);
    m_openPathEdit->setObjectName("startupPathEdit");
    m_openPathEdit->setPlaceholderText("Select a project file (.gvs)");
    m_openPathEdit->setMinimumHeight(50);
    QPushButton *browseButton = new QPushButton("...", startupPanel);
    browseButton->setObjectName("startupBrowseButton");
    browseButton->setFixedSize(54, 50);
    connect(browseButton, &QPushButton::clicked, this, &StartScreenDialog::OnBrowseOpen);
    openLayout->addWidget(m_openPathEdit, 1);
    openLayout->addWidget(browseButton);

    QPushButton *newButton = new QPushButton("Create New Project", startupPanel);
    QPushButton *openButton = new QPushButton("Open", startupPanel);
    QPushButton *cancelButton = new QPushButton("Cancel", startupPanel);
    const QList<QPushButton *> actionButtons = {newButton, openButton, cancelButton};
    for (QPushButton *button : actionButtons) {
        button->setObjectName("startupActionButton");
        button->setMinimumHeight(50);
        button->setMinimumWidth(button == newButton ? 190 : 150);
    }
    connect(openButton, &QPushButton::clicked, this, &StartScreenDialog::OnOpenProject);
    connect(newButton, &QPushButton::clicked, this, &StartScreenDialog::OnNewProject);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    openLayout->addWidget(newButton);
    openLayout->addWidget(openButton);
    openLayout->addWidget(cancelButton);
    startupLayout->addLayout(openLayout);

    rootLayout->addWidget(startupPanel);
}

void StartScreenDialog::SetupBackground()
{
    QString bgPath = FindAssetPath("assets/Backgrounds/StartingScreen2.png");
    if (bgPath.isEmpty()) {
        bgPath = FindAssetPath("assets/Backgrounds/StartingScreen.png");
    }

    const QString commonStyle =
        "QDialog#startScreen { background-color: #f8fafc; }"
        "QDialog#startScreen QWidget { background: transparent; color: #17472d; }"
        "QLabel#startupLogo { background: transparent; border: none; }"
        "QWidget#startupBrandDivider { background: rgba(37, 70, 51, 95); border: none; }"
        "QLabel#startupBrandName { color: #124a2b; background: transparent; border: none; }"
        "QLabel#startupBrandSubtitle { color: #4f5a60; background: transparent; border: none; }"
        "QWidget#startupPanel {"
        "  background: transparent;"
        "  border: none;"
        "  border-radius: 0px;"
        "  padding: 26px;"
        "}"
        "QLabel#startupTitle { color: #17472d; background: transparent; border: none; }"
        "QLabel#startupSubtitle { color: #5d6470; background: transparent; border: none; }"
        "QLineEdit#startupPathEdit {"
        "  background: rgba(255, 255, 255, 248);"
        "  color: #1f2933;"
        "  border: 1px solid #6d987b;"
        "  border-radius: 7px;"
        "  padding: 0 18px;"
        "  font-size: 15px;"
        "}"
        "QLineEdit#startupPathEdit:focus { border: 1px solid #245c37; background: #ffffff; }"
        "QLineEdit#startupPathEdit::placeholder { color: #8b949e; }"
        "QPushButton#startupBrowseButton, QPushButton#startupActionButton {"
        "  background: #ffffff;"
        "  color: #17472d;"
        "  border: 1px solid #6d987b;"
        "  border-radius: 7px;"
        "  padding: 0 18px;"
        "  font-size: 15px;"
        "  font-weight: 600;"
        "}"
        "QPushButton#startupBrowseButton:hover, QPushButton#startupActionButton:hover {"
        "  background: #e8f4eb;"
        "  border-color: #245c37;"
        "}"
        "QPushButton#startupBrowseButton:pressed, QPushButton#startupActionButton:pressed {"
        "  background: #d1e5d6;"
        "}";

    if (!bgPath.isEmpty()) {
        setStyleSheet(QString(
            "QDialog#startScreen {"
            " border-image: url(\"%1\") 0 0 0 0 stretch stretch;"
            "}"
            "%2").arg(bgPath, commonStyle));
    } else {
        setStyleSheet(commonStyle);
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
