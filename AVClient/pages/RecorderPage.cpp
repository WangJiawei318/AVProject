#include "RecorderPage.h"

#include "recorderdialog.h"

#include <QSizePolicy>
#include <QVBoxLayout>

RecorderPage::RecorderPage(QWidget *parent)
    : QWidget(parent),
      m_recorderDialog(new RecorderDialog(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    m_recorderDialog->setEmbeddedMode(true);
    m_recorderDialog->setWindowFlags(Qt::Widget);
    m_recorderDialog->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_recorderDialog);
    m_recorderDialog->show();
}
