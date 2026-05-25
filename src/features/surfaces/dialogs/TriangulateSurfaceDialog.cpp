#include "features/surfaces/dialogs/TriangulateSurfaceDialog.h"

#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>

TriangulateSurfaceDialog::TriangulateSurfaceDialog(const QStringList &existingLayers, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Triangulate Surface");
    setModal(true);
    resize(420, 150);

    QVBoxLayout *root = new QVBoxLayout(this);
    QFormLayout *form = new QFormLayout();

    m_targetCombo = new QComboBox(this);
    m_targetCombo->addItem("New layer");
    m_targetCombo->addItems(existingLayers);

    m_nameEdit = new QLineEdit("Triangulated Surface", this);

    m_dateEdit = new QDateEdit(QDate::currentDate(), this);
    m_dateEdit->setDisplayFormat("yyyy-MM-dd");
    m_dateEdit->setCalendarPopup(true);

    form->addRow("Target", m_targetCombo);
    form->addRow("Layer name", m_nameEdit);
    form->addRow("Date", m_dateEdit);
    root->addLayout(form);

    auto refreshTargetFields = [this]() {
        if (!m_nameEdit || !m_targetCombo) {
            return;
        }
        const bool newLayer = m_targetCombo->currentIndex() <= 0;
        m_nameEdit->setEnabled(newLayer);
        if (!newLayer) {
            m_nameEdit->clear();
            m_nameEdit->setPlaceholderText("Existing layer selected");
        } else if (m_nameEdit->text().trimmed().isEmpty()) {
            m_nameEdit->setPlaceholderText(QString());
            m_nameEdit->setText("Triangulated Surface");
        }
    };
    connect(m_targetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [refreshTargetFields](int) {
        refreshTargetFields();
    });
    refreshTargetFields();

    QDialogButtonBox *buttons = new QDialogButtonBox(this);
    buttons->addButton("Triangulate", QDialogButtonBox::AcceptRole);
    buttons->addButton("Cancel", QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &TriangulateSurfaceDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

TriangulateSurfaceDialog::Selection TriangulateSurfaceDialog::selection() const
{
    Selection out;
    if (m_targetCombo && m_targetCombo->currentIndex() > 0) {
        out.createNew = false;
        out.targetLayer = m_targetCombo->currentText().trimmed();
    } else {
        out.createNew = true;
        out.targetLayer = m_nameEdit ? m_nameEdit->text().trimmed() : QString();
    }
    out.date = m_dateEdit ? m_dateEdit->date() : QDate::currentDate();
    return out;
}

void TriangulateSurfaceDialog::accept()
{
    const Selection sel = selection();
    if (sel.targetLayer.isEmpty()) {
        QMessageBox::warning(this, "Triangulate Surface", "Enter a layer name.");
        return;
    }
    if (!sel.date.isValid()) {
        QMessageBox::warning(this, "Triangulate Surface", "Select a valid date.");
        return;
    }
    QDialog::accept();
}
