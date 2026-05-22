#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QPalette>
#include <QStyleFactory>
#include <QSurfaceFormat>

#include <QVTKOpenGLNativeWidget.h>

#include "gui/MainWindow.h"
#include "gui/StartScreenDialog.h"

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());

    QApplication app(argc, argv);
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    QIcon appIcon;
    const QString base = QCoreApplication::applicationDirPath();
    const QStringList iconNames = {
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

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(35, 35, 35));
    palette.setColor(QPalette::WindowText, QColor(220, 220, 220));
    palette.setColor(QPalette::Base, QColor(25, 25, 25));
    palette.setColor(QPalette::AlternateBase, QColor(30, 30, 30));
    palette.setColor(QPalette::Text, QColor(220, 220, 220));
    palette.setColor(QPalette::Button, QColor(45, 45, 45));
    palette.setColor(QPalette::ButtonText, QColor(220, 220, 220));
    palette.setColor(QPalette::Highlight, QColor(70, 130, 130));
    palette.setColor(QPalette::HighlightedText, QColor(240, 240, 240));
    app.setPalette(palette);

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
