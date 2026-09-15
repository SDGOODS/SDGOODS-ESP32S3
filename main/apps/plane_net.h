/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 应用层示例
 * https://github.com/SDGOODS/SDGOODS-ESP32S3
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 * 「谷仓共创计划」与「谷仓 SDGOODS 开放平台」项目、谷仓次元屏（谷仓电子徽章）设备，
 *   以及本基础代码的著作权与相关权利，均归深圳希德创新网络有限公司所有。
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 *
 * Required Notice: Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 *
 * 个人学习、研究与业余项目免费使用；商业用途需事前书面授权，见 LICENSING.md。
 * 分发时必须完整保留本声明 —— 这是许可条款，不是建议。
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* 飞机联机（蓝牙）通信模块
 * --------------------------------------------------------------
 * 复用 main.c 启动时已初始化的 BLE 栈（bluedroid 常驻），不再重复 init。
 * 双方设备广播同一个自定义 Service UUID 并互相发现；按 MAC 大小约定角色：
 *   - 本机 MAC < 对方 MAC  -> Client（主动 gattc_connect）
 *   - 本机 MAC > 对方 MAC  -> Server（等待连接）
 * 从而只建立一条连接，连接上双向交换状态包：
 *   Server: 通过 notify 发出本机状态；接收 Client 的 write（对端状态）
 *   Client: 通过 write  发出本机状态；接收 Server 的 notify（对端状态）
 *
 * 数据包（plane_peer_state_t，packed，13 字节）：
 *   x,y      对方飞机中心坐标（0..360）；未开始/未连接为 -1
 *   score    对方分数
 *   alive    1=游戏中 0=未开始/已阵亡
 *   level    对方关卡（共享进度显示用）
 *   start_seq 握手序号：对方点击开始后非零，用于同步开局
 */

typedef struct __attribute__((packed)) {
    int16_t   x;
    int16_t   y;
    int16_t   score;
    uint8_t   alive;
    uint8_t   level;
    uint8_t   start_seq;
    uint8_t   fire_seq;   /* 开火序号：本机每发出一发玩家子弹 +1，对端据此在绿机位置渲染对端子弹 */
    uint8_t   restart_seq; /* 重玩序号：死亡方点击重玩时 +1 并广播，对端检测到边沿即同步重开（双方回到同一局） */
    uint8_t   power;       /* 道具状态：本机火力等级 0..3（0=未增强，n=子弹列数 1+n）。
                              对端据此把一次开火复制成同样的列数与横向偏移，保证敌机一致。 */
    uint8_t   bomb_seq;    /* 炸弹序号：本机使用「全屏清除炸弹」时 +1，对端检测到边沿同步清屏（不计分） */
} plane_peer_state_t;

/* 收到对端状态时的回调（在 BLE 栈任务上下文调用，UI 内请自行转 LVGL 线程） */
typedef void (*plane_net_recv_cb_t)(const plane_peer_state_t *peer);
/* 连接状态变化回调：connected=true 已连接 / false 已断开（同样在 BLE 任务上下文） */
typedef void (*plane_net_conn_cb_t)(bool connected);

void plane_net_init(void);
/* 开始广播+扫描+连接。recv_cb 收到对端状态，conn_cb 连接变化（可 NULL） */
void plane_net_begin(plane_net_recv_cb_t recv_cb, plane_net_conn_cb_t conn_cb);
/* 提交本机最新状态（非阻塞，内部在连接上发出） */
void plane_net_send(const plane_peer_state_t *st);
/* 停止联机并恢复 BLE GAP 回调（供扫描页使用） */
void plane_net_stop(void);
/* 是否已连接 */
bool plane_net_connected(void);
/* 协商得到的共享随机种子（连接成功后有效，否则 0） */
uint32_t plane_net_seed(void);
/* 读取对端最新状态（线程安全复制） */
void plane_net_get_peer(plane_peer_state_t *out);
