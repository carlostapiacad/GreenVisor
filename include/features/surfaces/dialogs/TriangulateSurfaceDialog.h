#pragma once

#include <QDate>
#include <QDialog>
#include <QString>
#include <QStringList>

class QComboBox;
class QDateEdit;
class QLineEdit;

class TriangulateSurfaceDialog : public QDialog
{
public:
    struct Selection
    {
        QString targetLayer;
        QDate date;
        bool createNew = true;
    };

    explicit TriangulateSurfaceDialog(const QStringList &existingLayers, QWidget *parent = nullptr);

    Selection selection() const;

protected:
    void accept() override;

private:
    QComboBox *m_targetCombo = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QDateEdit *m_dateEdit = nullptr;
};
