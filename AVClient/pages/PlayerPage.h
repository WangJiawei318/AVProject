#ifndef PLAYERPAGE_H
#define PLAYERPAGE_H

#include <QWidget>

class PlayerDialog;

class PlayerPage : public QWidget
{
    Q_OBJECT

public:
    explicit PlayerPage(QWidget *parent = nullptr);

public slots:
    void playLocalFile(const QString &filePath);

private:
    PlayerDialog *m_playerDialog;
};

#endif // PLAYERPAGE_H
