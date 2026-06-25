#ifndef RECORDERPAGE_H
#define RECORDERPAGE_H

#include <QWidget>

class RecorderDialog;

class RecorderPage : public QWidget
{
    Q_OBJECT

public:
    explicit RecorderPage(QWidget *parent = nullptr);

private:
    RecorderDialog *m_recorderDialog;
};

#endif // RECORDERPAGE_H
