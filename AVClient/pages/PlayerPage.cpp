#include "PlayerPage.h"

#include "playerdialog.h"

#include <QSizePolicy>
#include <QVBoxLayout>

PlayerPage::PlayerPage(QWidget *parent)
    : QWidget(parent),
      m_playerDialog(new PlayerDialog(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    m_playerDialog->setWindowFlags(Qt::Widget);
    m_playerDialog->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_playerDialog);
    m_playerDialog->show();
}
