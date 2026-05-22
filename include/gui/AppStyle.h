#pragma once

class QApplication;
class QString;
class QWidget;

namespace AppStyle
{
void ApplyApplicationStyle(QApplication &app);
void ApplyWidgetStyle(QWidget *widget);
QString LoadMainStyleSheet();
}
