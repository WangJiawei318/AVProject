#ifndef PACKETQUEUE_H
#define PACKETQUEUE_H

/*
 * packetqueue.h
 * ------------------------------------------------------------
 * AVPacket 缓冲队列。
 *
 * 为什么需要队列？
 * FFmpeg 的 av_read_frame() 负责从文件中读取压缩后的音频/视频 packet。
 * 但是读取文件、音频解码、视频解码不是同一个节奏：
 * 1. 读线程负责不断读取 packet；
 * 2. 音频 SDL callback 在 SDL 的音频线程中取音频 packet 解码；
 * 3. 视频线程或 SDL 定时器取视频 packet 解码。
 *
 * 所以中间必须有两个队列：
 * audioq：缓存音频 packet；
 * videoq：缓存视频 packet。
 *
 * 队列使用 SDL_mutex + SDL_cond 做线程同步，避免多个线程同时读写链表导致崩溃。
 */

#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <SDL.h>
#ifdef __cplusplus
}
#endif

// 链表节点：每个节点保存一个 AVPacket。
// 这里不要直接保存指针，因为 av_read_frame() 循环会不断复用外部 packet。
// packet_queue_put() 会用 av_packet_ref() 复制 packet 引用计数。
typedef struct PacketListNode
{
    AVPacket pkt;                 // 压缩后的音频或视频数据包
    struct PacketListNode *next;  // 指向下一个 packet 节点
} PacketListNode;

// PacketQueue 是一个线程安全队列。
typedef struct PacketQueue
{
    PacketListNode *first_pkt;    // 队头：解码线程从这里取 packet
    PacketListNode *last_pkt;     // 队尾：读线程向这里放 packet
    int nb_packets;               // 队列中 packet 数量
    int size;                     // 队列中压缩数据总字节数，用于控制缓存大小
    SDL_mutex *mutex;             // 互斥锁，保护链表读写
    SDL_cond *cond;               // 条件变量，block 模式下用于等待新 packet
} PacketQueue;

// 初始化队列，创建 mutex 和 cond。
void packet_queue_init(PacketQueue *queue);

// 向队列尾部放入一个 packet。
// 成功返回 0，失败返回 -1。
int  packet_queue_put(PacketQueue *queue, AVPacket *packet);

// 从队列头部取出一个 packet。
// block=0：队列空时立即返回 0；
// block=1：队列空时等待，直到有新 packet。
// 成功取到 packet 返回 1。
int  packet_queue_get(PacketQueue *queue, AVPacket *packet, int block);

// 清空队列中所有 packet，但保留 mutex/cond，队列还能继续使用。
// seek 跳转时会用到：跳转后旧 packet 必须全部丢弃。
void packet_queue_flush(PacketQueue *queue);

// 销毁队列资源：先 flush，再销毁 mutex/cond。
void packet_queue_destroy(PacketQueue *queue);

#endif // PACKETQUEUE_H
