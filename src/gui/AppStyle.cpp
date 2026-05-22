#include "gui/AppStyle.h"

#include "gui/GuiUtils.h"

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QIODevice>
#include <QPalette>
#include <QString>
#include <QStyleFactory>
#include <QWidget>

namespace
{
QPalette LightPalette()
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(248, 250, 252));
    palette.setColor(QPalette::WindowText, QColor(31, 41, 51));
    palette.setColor(QPalette::Base, QColor(255, 255, 255));
    palette.setColor(QPalette::AlternateBase, QColor(241, 245, 249));
    palette.setColor(QPalette::ToolTipBase, QColor(255, 255, 255));
    palette.setColor(QPalette::ToolTipText, QColor(31, 41, 51));
    palette.setColor(QPalette::Text, QColor(31, 41, 51));
    palette.setColor(QPalette::Button, QColor(255, 255, 255));
    palette.setColor(QPalette::ButtonText, QColor(31, 41, 51));
    palette.setColor(QPalette::BrightText, QColor(255, 255, 255));
    palette.setColor(QPalette::Highlight, QColor(219, 234, 254));
    palette.setColor(QPalette::HighlightedText, QColor(15, 23, 42));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(148, 163, 184));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(148, 163, 184));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(148, 163, 184));
    return palette;
}

QString FallbackStyleSheet()
{
    return QStringLiteral(
        "QMainWindow, QDialog { background-color: #f8fafc; color: #1f2933; }"
        "QMenuBar { background: #f8fafc; color: #1f2933; border-bottom: 1px solid #d7dde5; }"
        "QMenu { background: #ffffff; color: #1f2933; border: 1px solid #cfd7e3; }"
        "QTreeView, QTextEdit, QLineEdit, QComboBox, QTableWidget, QListWidget {"
        "  background: #ffffff; border: 1px solid #cfd7e3; color: #1f2933;"
        "  selection-background-color: #dbeafe; selection-color: #0f172a;"
        "}"
        "QComboBox QAbstractItemView { background: #ffffff; color: #1f2933; }"
        "QPushButton { background: #ffffff; color: #1f2933; border: 1px solid #cfd7e3; border-radius: 4px; padding: 4px 8px; }");
}
}

namespace AppStyle
{
QString LoadMainStyleSheet()
{
    QFile styleFile(FindAssetPath("assets/styles/main.qss"));
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString::fromUtf8(styleFile.readAll());
    }
    return FallbackStyleSheet();
}

void ApplyApplicationStyle(QApplication &app)
{
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    app.setPalette(LightPalette());
    app.setStyleSheet(LoadMainStyleSheet());
}

void ApplyWidgetStyle(QWidget *widget)
{
    if (!widget) {
        return;
    }
    widget->setStyleSheet(LoadMainStyleSheet());
}
}
