#include "packetqueue.h"

#include <cstring>

/*
 * packet_queue_init
 * ------------------------------------------------------------
 * 初始化 PacketQueue。
 *
 * 使用前必须先调用这个函数，否则 first_pkt、last_pkt、mutex 等字段未初始化，
 * 后续 put/get 会出现野指针或线程同步错误。
 */
void packet_queue_init(PacketQueue *queue)
{
    if (!queue) return;

    // 清零后，first_pkt/last_pkt 为 nullptr，nb_packets/size 为 0。
    std::memset(queue, 0, sizeof(PacketQueue));

    // SDL 互斥锁：保证读线程和解码线程不会同时改链表。
    queue->mutex = SDL_CreateMutex();

    // SDL 条件变量：block 模式取数据时，如果队列为空，可以等待 put 唤醒。
    queue->cond = SDL_CreateCond();
}

/*
 * packet_queue_put
 * ------------------------------------------------------------
 * 将一个 AVPacket 放入队列。
 *
 * 关键点：这里使用 av_packet_ref()，不是简单结构体赋值。
 * 原因：AVPacket 内部 data/buf 使用引用计数。如果只是浅拷贝，外部 av_packet_unref()
 * 后，队列里的 packet 可能指向已经释放的数据。
 */
int packet_queue_put(PacketQueue *queue, AVPacket *packet)
{
    if (!queue || !packet || !queue->mutex) return -1;

    PacketListNode *node = static_cast<PacketListNode *>(av_mallocz(sizeof(PacketListNode)));
    if (!node) return -1;

    av_init_packet(&node->pkt);

    // 增加 packet 内部 buffer 的引用计数，让队列拥有一份可独立释放的引用。
    if (av_packet_ref(&node->pkt, packet) < 0)
    {
        av_free(node);
        return -1;
    }

    node->next = nullptr;

    SDL_LockMutex(queue->mutex);

    // 队列为空：新节点既是队头也是队尾。
    if (queue->last_pkt == nullptr)
        queue->first_pkt = node;
    else
        queue->last_pkt->next = node;

    queue->last_pkt = node;
    queue->nb_packets++;
    queue->size += node->pkt.size;

    // 如果有线程正在 packet_queue_get(..., block=1) 中等待，唤醒它。
    SDL_CondSignal(queue->cond);
    SDL_UnlockMutex(queue->mutex);

    return 0;
}

/*
 * packet_queue_get
 * ------------------------------------------------------------
 * 从队列头部取出一个 AVPacket。
 *
 * block=0：非阻塞模式。队列为空时立即返回 0。
 * block=1：阻塞模式。队列为空时等待条件变量。
 *
 * 本项目当前大部分位置使用 block=0，配合 SDL_Delay()，这样更容易处理 quit、pause、
 * readFinished 等状态，不容易在停止播放时卡死。
 */
int packet_queue_get(PacketQueue *queue, AVPacket *packet, int block)
{
    if (!queue || !packet || !queue->mutex) return -1;

    int ret = 0;

    SDL_LockMutex(queue->mutex);

    for (;;)
    {
        PacketListNode *node = queue->first_pkt;
        if (node)
        {
            // 取出队头节点。
            queue->first_pkt = node->next;
            if (queue->first_pkt == nullptr)
                queue->last_pkt = nullptr;

            queue->nb_packets--;
            queue->size -= node->pkt.size;

            // 将 AVPacket 引用转移给调用者。
            // 调用者解码完后必须 av_packet_unref(packet)。
            *packet = node->pkt;
            av_free(node);
            ret = 1;
            break;
        }
        else if (!block)
        {
            // 非阻塞模式：没有数据直接返回。
            ret = 0;
            break;
        }
        else
        {
            // 阻塞模式：等待 packet_queue_put() 调用 SDL_CondSignal()。
            SDL_CondWait(queue->cond, queue->mutex);
        }
    }

    SDL_UnlockMutex(queue->mutex);
    return ret;
}

/*
 * packet_queue_flush
 * ------------------------------------------------------------
 * 清空队列。
 *
 * 典型使用场景：seek 跳转。
 * 跳转后，队列中原位置的音视频 packet 已经无效，必须全部释放，否则会出现：
 * 1. 跳转后仍继续播放旧位置的几秒数据；
 * 2. 视频花屏；
 * 3. 音画时间戳混乱。
 */
void packet_queue_flush(PacketQueue *queue)
{
    if (!queue || !queue->mutex) return;

    SDL_LockMutex(queue->mutex);

    PacketListNode *node = queue->first_pkt;
    while (node)
    {
        PacketListNode *next = node->next;
        av_packet_unref(&node->pkt);  // 释放 AVPacket 内部引用
        av_free(node);                // 释放链表节点
        node = next;
    }

    queue->first_pkt = nullptr;
    queue->last_pkt = nullptr;
    queue->nb_packets = 0;
    queue->size = 0;

    SDL_UnlockMutex(queue->mutex);
}

/*
 * packet_queue_destroy
 * ------------------------------------------------------------
 * 销毁队列。
 *
 * 注意：这个函数只销毁队列内部资源，不 delete queue 指针本身。
 * 因为 queue 是在 VideoPlayer::run() 中 new 出来的，所以外层还需要 delete。
 */
void packet_queue_destroy(PacketQueue *queue)
{
    if (!queue) return;

    packet_queue_flush(queue);

    if (queue->mutex)
    {
        SDL_DestroyMutex(queue->mutex);
        queue->mutex = nullptr;
    }

    if (queue->cond)
    {
        SDL_DestroyCond(queue->cond);
        queue->cond = nullptr;
    }
}
