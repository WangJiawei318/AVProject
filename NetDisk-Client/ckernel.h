#ifndef CKERNEL_H
#define CKERNEL_H

#include <QObject>
#include"maindialog.h"
#include"packdef.h"

//核心处理类

//单例
//1.构造 拷贝构造 析构 私有化  2.提供静态的公有的获取对象的方法
//#include<INetMediator.h>
class INetMediator;

//#define USE_SERVER 1

class CKernel : public QObject
{
    Q_OBJECT
private:
    explicit CKernel(QObject *parent = nullptr);
    explicit CKernel(const CKernel & kernel){}
    ~CKernel(){}

    void loadInitFile();
signals:

public:
    static CKernel* GetInstance(){
        static CKernel kernel;
        return &kernel;
    }

private slots:
    ///普通槽函数
    void slot_destory();

    ///网络响应槽函数
    void slot_dealClientData(unsigned int lSendIP, char* buf, int nlen);

#ifdef USE_SERVER
    void slot_dealServerData(unsigned int lSendIP, char* buf, int nlen);
#endif

private:
    MainDialog * m_mainDialog;

    QString m_ip;
    QString m_port;

    INetMediator *m_tcpClient;
#ifdef USE_SERVER
    INetMediator *m_tcpServer;
#endif

};

#endif // CKERNEL_H
