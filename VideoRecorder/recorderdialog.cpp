#include "recorderdialog.h"
#include "ui_recorderdialog.h"

RecorderDialog::RecorderDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::RecorderDialog)
{
    ui->setupUi(this);

    m_pictureWidget = new PictureWidget;
    m_pictureWidget->hide();
    m_pictureWidget->move(0,0);

    this->setWindowFlags(Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);

    m_saveFileThread = new SaveVideoFileThread;
    connect(m_saveFileThread,SIGNAL(SIG_sendPicInPic(QImage)),m_pictureWidget,SLOT(slot_setImage(QImage)));
    connect(m_saveFileThread,SIGNAL(SIG_sendVideoFrame(QImage)),this,SLOT(slot_setImage(QImage)));
}

RecorderDialog::~RecorderDialog()
{
    delete ui;
}


void RecorderDialog::on_pb_start_clicked()
{
    this->showMinimized();
    m_pictureWidget->show();

    STRU_AV_FORMAT format;

    format.fileName = m_saveUrl;
    format.frame_rate = FRAME_RATE;
    format.hasAudio = true;
    format.hasCamera = true;
    format.hasDesk = true;
    format.videoBitRate = 1400000;
    //采样频率 44100
    //码率 64000
    //声道 2
    //精度位数 16
    //... fltp aac ...

    QScreen *src = QApplication::primaryScreen();
    QRect rect = src->geometry();
    format.width = rect.width();
    format.height = rect.height();

    m_saveFileThread->slot_setInfo(format);
    m_saveFileThread->slot_openVideo();
}


void RecorderDialog::on_pb_stop_clicked()
{
    m_pictureWidget->hide();
    m_saveFileThread->slot_closeVideo();
}


void RecorderDialog::on_pb_setUrl_clicked()
{
    //ui->pb_start->setEnabled(true);
    m_saveUrl = ui->le_url->text();
    m_saveUrl = m_saveUrl.replace("/","\\");
}

void RecorderDialog::slot_setImage(QImage img)
{
    QPixmap pixmap;
    if(!img.isNull()){
        pixmap = QPixmap::fromImage(img.scaled(ui->lb_showImage->size(),Qt::KeepAspectRatio));
    }else{
        pixmap = QPixmap::fromImage(img);
    }
    ui->lb_showImage->setPixmap(pixmap);
}
