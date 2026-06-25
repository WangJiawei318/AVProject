#include "SettingsPage.h"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

SettingsPage::SettingsPage(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);

    auto *server = new QLineEdit(this);
    server->setPlaceholderText(tr("阶段 2 接入服务器地址"));
    server->setEnabled(false);

    auto *cache = new QLineEdit(this);
    cache->setPlaceholderText(tr("阶段 4 下载缓存目录"));
    cache->setEnabled(false);

    form->addRow(tr("服务器"), server);
    form->addRow(tr("缓存目录"), cache);

    auto *hint = new QLabel(tr("设置页面为阶段 1 占位，当前只整合本地播放和本地录制。"), this);
    hint->setWordWrap(true);

    root->addLayout(form);
    root->addWidget(hint);
    root->addStretch();
}
