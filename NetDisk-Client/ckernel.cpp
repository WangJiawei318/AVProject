#include "ckernel.h"
#include<QDebug>
#include<qcoreapplication>
#include<QFileInfo>
//配置文件使用的类
#include<QSettings>
#include<QMessageBox>
#include"TcpClientMediator.h"
#include"TcpServerMediator.h"

CKernel::CKernel(QObject *parent) : QObject(parent)
{
    //加载配置文件
    loadInitFile();

#ifdef USE_SERVER
    m_tcpServer = new TcpServerMediator;
    connect(m_tcpServer,SIGNAL(SIG_ReadyData(uint,char*,int)),this,SLOT(slot_dealServerData(uint,char*,int)));
    m_tcpServer->OpenNet();
#endif

    m_tcpClient = new TcpClientMediator;
    connect(m_tcpClient,SIGNAL(SIG_ReadyData(uint,char*,int)),this,SLOT(slot_dealClientData(uint,char*,int)));

    //客户端连接真实地址
    m_tcpClient->OpenNet("192.168.44.130");

    m_mainDialog = new MainDialog;

    connect(m_mainDialog,SIGNAL(SIG_close()),this,SLOT(slot_destory()) );
    m_mainDialog->show();

#ifdef USE_SERVER
    //测试 对服务器发送数据
    char strBuf[100] = "hello server";
    int len = strlen("hello server")+1;
    m_tcpClient->SendData(0,strBuf,len);   //客户端一定是发给服务器的，套接字参数随意
#endif

    STRU_LOGIN_RQ rq;
    m_tcpClient->SendData(0,(char*)&rq,sizeof(rq));
}

void CKernel::loadInitFile()
{
    //默认值
    m_ip = "192.168.44.130";
    m_port = "8004";

    //获取exe目录
    QString path = QCoreApplication::applicationDirPath() + "/config.ini";
    //根据目录 看文件是否存在 存在加载 不存在创建并且写入默认值
    QFileInfo info(path);
    if(info.exists()){
        //存在
        QSettings setting(path,QSettings::IniFormat);
        //打开组
        setting.beginGroup("net");
        QVariant strIP = setting.value("ip","");
        QVariant strPORT = setting.value("prot","");
        if(!strIP.toString().isEmpty()) m_ip = strIP.toString();
        if(!strPORT.toString().isEmpty()) m_port = strPORT.toString();
        //关闭组
        setting.endGroup();
    }else{
        //不存在
        QSettings setting(path,QSettings::IniFormat);   //没有会创建
        //打开组
        setting.beginGroup("net");
        //设置 key value
        setting.setValue("ip",m_ip);
        setting.setValue("port",m_port);
        //关闭组
        setting.endGroup();
    }
    qDebug()<<"ip:"<<m_ip<<" port:"<<m_port;
}

void CKernel::slot_destory(){
    qDebug()<<__func__;
    delete m_mainDialog;
}
#include<QDebug>
//客户端处理数据
void CKernel::slot_dealClientData(unsigned int lSendIP, char *buf, int nlen)
{
    //QString str = QString("来自服务端:%1").arg(QString::fromStdString(buf));
    //QMessageBox::about(NULL,"提示",str);  //about 阻塞的 模态窗口

    int type = *(int*)buf;
    qDebug()<<__func__;

    //回收空间
    delete[] buf;
}
#ifdef USE_SERVER
void CKernel::slot_dealServerData(unsigned int lSendIP, char *buf, int nlen)
{
    QString str = QString("来自客户端:%1").arg(QString::fromStdString(buf));
    QMessageBox::about(NULL,"提示",str);  //about 阻塞的 模态窗口

    m_tcpServer->SendData(lSendIP,buf,nlen);

    //回收空间
    delete[] buf;
}
#endif

//配置文件  放在什么位置？——> exe同级目录  思路：根据目录，看文件是否存在，存在就加载，不存在就创建并且写入默认值
//  .ini
//格式
//[组名]
//key=value

//例如
//[net]
//ip=192.168.44.130
//port=8004
