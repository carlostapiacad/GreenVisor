#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QSurfaceFormat>

#include <QVTKOpenGLNativeWidget.h>

#include "gui/AppStyle.h"
#include "gui/MainWindow.h"
#include "gui/StartScreenDialog.h"

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());

    QApplication app(argc, argv);
    AppStyle::ApplyApplicationStyle(app);

    QIcon appIcon;
    const QString base = QCoreApplication::applicationDirPath();
    const QStringList logoCandidates = {
        QDir(base).filePath("../assets/Icons/Logo.png"),
        QDir(base).filePath("../../assets/Icons/Logo.png"),
        QDir("assets/Icons").filePath("Logo.png")};
    for (const QString &path : logoCandidates) {
        if (QFile::exists(path)) {
            appIcon.addFile(path);
            break;
        }
    }
    const QStringList iconNames = {
        "GreenVisor.ico",
        "icon16x16.ico",
        "icon32x32.ico",
        "icon48x48.ico",
        "icon256x256.ico"};
    for (const QString &name : iconNames) {
        QString path = QDir(base).filePath("../assets/Icons/" + name);
        if (!QFile::exists(path)) {
            path = QDir(base).filePath("../assets/ICON/" + name);
        }
        if (!QFile::exists(path)) {
            path = QDir("assets/Icons").filePath(name);
        }
        if (QFile::exists(path)) {
            appIcon.addFile(path);
        }
    }
    if (!appIcon.isNull()) {
        app.setWindowIcon(appIcon);
    }

    StartScreenDialog startDialog;
    if (startDialog.exec() != QDialog::Accepted) {
        return 0;
    }

    MainWindow window;
    if (startDialog.action() == StartScreenDialog::Action::Open) {
        window.LoadProject(startDialog.projectPath());
    } else if (startDialog.action() == StartScreenDialog::Action::New) {
        window.NewProject(startDialog.projectPath());
    }
    if (!appIcon.isNull()) {
        window.setWindowIcon(appIcon);
    }
    window.resize(1280, 800);
    window.show();

    return app.exec();
}
