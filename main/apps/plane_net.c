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

#include "plane_net.h"

#include <string.h>
#include <stdlib.h>
#include "sdgoods_ble.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_defs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "plane_net";

/* ======================== 自定义 UUID ======================== */
/* 128-bit Service / Characteristic UUID（小端字节序与本机广播一致即可） */
static const uint8_t PLANE_SVC_UUID128[16] = {
    0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88
};
static const uint8_t PLANE_CHAR_UUID128[16] = {
    0x21, 0x43, 0x65, 0x87, 0xA9, 0xCB, 0xED, 0xF0,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x89
};

#define GATTS_APP_ID  0x0182
#define GATTC_APP_ID  0x0183
#define PROFILE_NUM   1

#define PLANE_PKT_LEN (sizeof(plane_peer_state_t))   /* plane_peer_state_t packed 大小(11字节) */
#define PLANE_INVALID_HANDLE 0x0000

typedef enum { ROLE_NONE, ROLE_SERVER, ROLE_CLIENT } role_t;

/* ======================== 全局状态 ======================== */
static bool              s_inited       = false;
static bool              s_active       = false;   /* begin 之后为 true */
static role_t            s_role         = ROLE_NONE;
static esp_gatt_if_t     s_gatts_if     = 0;
static esp_gatt_if_t     s_gattc_if     = 0;
static bool              s_gatts_reg    = false;
static bool              s_gattc_reg    = false;
static uint16_t          s_svc_handle   = 0;
static uint16_t          s_char_handle  = 0;
static uint16_t          s_descr_handle = 0;

/* Client 侧发现到的服务起止句柄 */
static uint16_t          s_svc_start    = 0;
static uint16_t          s_svc_end      = 0;

static uint16_t          s_gatts_conn_id = 0;
static uint16_t          s_gattc_conn_id = 0;
static bool              s_connected     = false;
/* 协议栈层链路是否真的建立（与 s_connected 不同：s_connected 还含 notify 订阅完成）。
   退出游戏时必须主动断开，否则对端仍处「已连接」：
   Server 侧 GATTS_CONNECT_EVT 不再触发、Client 侧 esp_ble_gattc_open 会因「已连接」失败，
   表现为「进入→退出→再进入就搜不到对方，只能重启」。 */
static bool              s_link_up       = false;
static bool              s_link_is_client = false;   /* 该链路由本方以 Client 身份发起 */

static bool              s_adv_on = false;
static bool              s_scan_on = false;
/* s_write_pending 已移除：NO_RSP 写不门控，客户端每 tick 直接发送本机状态 */

static uint8_t           s_own_addr[6] = {0};
static uint8_t           s_peer_addr[6] = {0};

static uint32_t          s_seed = 0;

static plane_peer_state_t s_peer = { .x = -1, .y = -1, .score = 0, .alive = 0, .level = 1, .start_seq = 0 };
static plane_peer_state_t s_local = { .x = -1, .y = -1, .score = 0, .alive = 0, .level = 1, .start_seq = 0 };
static uint8_t           s_tx_buf[PLANE_PKT_LEN];

/* GATTS 特征值 / CCCD 的属性值缓冲（auto_rsp 不能为 NULL） */
static uint8_t           s_char_val_buf[PLANE_PKT_LEN];
static uint8_t           s_cccd_val_buf[2] = {0, 0};
static esp_attr_value_t  s_char_val = {
    .attr_max_len = PLANE_PKT_LEN,
    .attr_len     = PLANE_PKT_LEN,
    .attr_value   = s_char_val_buf,
};
static esp_attr_value_t  s_cccd_val = {
    .attr_max_len = 2,
    .attr_len     = 2,
    .attr_value   = s_cccd_val_buf,
};

static plane_net_recv_cb_t s_recv_cb = NULL;
static plane_net_conn_cb_t s_conn_cb = NULL;

/* ======================== 工具 ======================== */
static void pack_state(const plane_peer_state_t *s, uint8_t *buf)
{
    memcpy(buf, s, PLANE_PKT_LEN);
}
static void unpack_state(const uint8_t *buf, plane_peer_state_t *s)
{
    memcpy(s, buf, PLANE_PKT_LEN);
}

static int addr_cmp(const uint8_t *a, const uint8_t *b)
{
    for (int i = 5; i >= 0; i--) {
        if (a[i] != b[i]) return (a[i] < b[i]) ? -1 : 1;
    }
    return 0;
}

static uint32_t addr_to_u32(const uint8_t *a)
{
    return ((uint32_t)a[5] << 24) | ((uint32_t)a[4] << 16) |
           ((uint32_t)a[3] << 8) | (uint32_t)a[2];
}

static void start_adv(void)
{
    if (s_adv_on) return;
    esp_ble_adv_data_t adv = {
        .set_scan_rsp = false,
        .include_name = false,
        .include_txpower = false,
        .min_interval = 0x100,
        .max_interval = 0x100,
        .appearance = 0x00,
        .manufacturer_len = 0,
        .p_manufacturer_data = NULL,
        .service_data_len = 0,
        .p_service_data = NULL,
        .service_uuid_len = 16,
        .p_service_uuid = (uint8_t *)PLANE_SVC_UUID128,
        .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
    };
    esp_ble_gap_config_adv_data(&adv);
}

static void start_scan(void)
{
    if (s_scan_on) return;
    esp_ble_scan_params_t params = {
        .scan_type = BLE_SCAN_TYPE_ACTIVE,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval = 0x50,
        .scan_window = 0x30,
        .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
    };
    if (esp_ble_gap_set_scan_params(&params) == ESP_OK) {
        if (esp_ble_gap_start_scanning(0) == ESP_OK) {   /* 0 = 持续扫描，连接后主动停止 */
            s_scan_on = true;   /* 原实现漏置位 → stop_scan() 永远空转、扫描停不掉 */
        }
    }
}

static void stop_adv(void)
{
    if (s_adv_on) {
        esp_ble_gap_stop_advertising();
        s_adv_on = false;
    }
}
static void stop_scan(void)
{
    if (s_scan_on) {
        esp_ble_gap_stop_scanning();
        s_scan_on = false;
    }
}

/* 实际把本机状态发出去（在连接上） */
static void net_send_now(void)
{
    if (!s_connected) return;
    pack_state(&s_local, s_tx_buf);
    if (s_role == ROLE_SERVER) {
        esp_ble_gatts_send_indicate(s_gatts_if, s_gatts_conn_id,
                                    s_char_handle, PLANE_PKT_LEN, s_tx_buf, false);
    } else if (s_role == ROLE_CLIENT) {
        /* NO_RSP 写不会触发 GATTC_WRITE_CHAR_EVT，若用 s_write_pending 门控，
           客户端只会发出第一帧，导致对方永远看不到本方飞机（x=-1 被隐藏）。
           改为每 tick 直接发送，与 SERVER 侧 indicate 的未门控发送保持一致。 */
        esp_ble_gattc_write_char(s_gattc_if, s_gattc_conn_id,
                                 s_char_handle, PLANE_PKT_LEN, s_tx_buf,
                                 ESP_GATT_WRITE_TYPE_NO_RSP,
                                 ESP_GATT_AUTH_REQ_NONE);
    }
}

/* ======================== GAP 回调 ======================== */
static void gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    if (!s_active) return;
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&(esp_ble_adv_params_t){
            .adv_int_min = 0x100,
            .adv_int_max = 0x100,
            .adv_type = ADV_TYPE_IND,
            .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
            .channel_map = ADV_CHNL_ALL,
            .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
        });
        s_adv_on = true;
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGW(TAG, "adv start fail %d", param->adv_start_cmpl.status);
        }
        break;

    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        if (param->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) break;
        if (s_connected) break;
        uint8_t len = 0;
        uint8_t *adv = (uint8_t *)param->scan_rst.ble_adv;
        uint16_t adv_len = param->scan_rst.adv_data_len;
        uint8_t *uuid = esp_ble_resolve_adv_data_by_type(
            adv, adv_len, ESP_BLE_AD_TYPE_128SRV_CMPL, &len);
        if (!uuid || len != 16) break;
        if (memcmp(uuid, PLANE_SVC_UUID128, 16) != 0) break;
        /* 发现对方：按 MAC 决定角色 */
        const uint8_t *peer = param->scan_rst.bda;
        int cmp = addr_cmp(s_own_addr, peer);
        if (cmp < 0) {
            /* 本机 MAC 较小 -> Client，主动连接 */
            s_role = ROLE_CLIENT;
            memcpy(s_peer_addr, peer, 6);
            stop_scan();
            ESP_LOGI(TAG, "client connect to peer");
            esp_ble_gattc_open(s_gattc_if, (uint8_t *)peer, BLE_ADDR_TYPE_PUBLIC, true);
        } else {
            /* 本机 MAC 较大 -> Server，停止扫描等待连接 */
            s_role = ROLE_SERVER;
            stop_scan();
            ESP_LOGI(TAG, "server role, waiting for connection");
        }
        break;
    }

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        s_scan_on = false;
        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        s_adv_on = false;
        break;

    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        ESP_LOGI(TAG, "auth complete status=%d", param->ble_security.auth_cmpl.success);
        break;

    default:
        break;
    }
}

/* ======================== GATTS（Server）回调 ======================== */
static void gatts_cb(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                     esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT:
        s_gatts_if = gatts_if;
        s_gatts_reg = true;
        {
            esp_bt_uuid_t svc_uuid = { .len = ESP_UUID_LEN_128, .uuid = { .uuid128 = {0} } };
            memcpy(svc_uuid.uuid.uuid128, PLANE_SVC_UUID128, 16);
            esp_gatt_srvc_id_t svc_id = {
                .id = { .uuid = svc_uuid, .inst_id = 0 },
                .is_primary = true,
            };
            esp_ble_gatts_create_service(gatts_if, &svc_id, 4);
        }
        break;

    case ESP_GATTS_CREATE_EVT:
        s_svc_handle = param->create.service_handle;
        {
            esp_bt_uuid_t chr = { .len = ESP_UUID_LEN_128, .uuid = { .uuid128 = {0} } };
            memcpy(chr.uuid.uuid128, PLANE_CHAR_UUID128, 16);
            esp_gatt_perm_t perm = ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE;
            esp_gatt_char_prop_t prop = ESP_GATT_CHAR_PROP_BIT_READ |
                                         ESP_GATT_CHAR_PROP_BIT_WRITE |
                                         ESP_GATT_CHAR_PROP_BIT_NOTIFY;
            esp_attr_control_t ctr = { .auto_rsp = ESP_GATT_AUTO_RSP };
            esp_ble_gatts_add_char(s_svc_handle, &chr, perm, prop, &s_char_val, &ctr);
        }
        break;

    case ESP_GATTS_ADD_CHAR_EVT:
        s_char_handle = param->add_char.attr_handle;
        {
            esp_bt_uuid_t cccd = { .len = ESP_UUID_LEN_16, .uuid = { .uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG } };
            esp_attr_control_t ctr = { .auto_rsp = ESP_GATT_AUTO_RSP };
            esp_ble_gatts_add_char_descr(s_svc_handle, &cccd,
                                         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                         &s_cccd_val, &ctr);
        }
        break;

    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        s_descr_handle = param->add_char_descr.attr_handle;
        esp_ble_gatts_start_service(s_svc_handle);
        break;

    case ESP_GATTS_START_EVT:
        break;

    case ESP_GATTS_CONNECT_EVT:
        /* 已退出游戏后才到达的连接（如退出瞬间对方刚好连上）：立刻断开，
           避免留下「幽灵链路」导致下次进入搜不到对端 */
        if (!s_active) {
            ESP_LOGW(TAG, "GATTS late connect while inactive, close it");
            esp_ble_gatts_close(gatts_if, param->connect.conn_id);
            break;
        }
        s_gatts_conn_id = param->connect.conn_id;
        s_link_up = true;
        s_link_is_client = false;
        memcpy(s_peer_addr, param->connect.remote_bda, 6);
        s_seed = addr_to_u32(s_own_addr) ^ addr_to_u32(param->connect.remote_bda);
        s_connected = true;
        stop_adv();
        stop_scan();
        ESP_LOGI(TAG, "GATTS connected seed=0x%08X", s_seed);
        if (s_conn_cb) s_conn_cb(true);
        break;

    case ESP_GATTS_WRITE_EVT:
        if (param->write.handle == s_char_handle && param->write.len == PLANE_PKT_LEN) {
            plane_peer_state_t p;
            unpack_state(param->write.value, &p);
            memcpy(&s_peer, &p, PLANE_PKT_LEN);
            { static uint32_t n = 0; if ((n++ % 90) == 0)
                ESP_LOGI(TAG, "SRV rx #%u x=%d y=%d alive=%d score=%d rseq=%d",
                         (unsigned)n, p.x, p.y, p.alive, p.score, p.restart_seq); }
            if (s_recv_cb) s_recv_cb(&s_peer);
        }
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        s_connected = false;
        s_link_up = false;
        s_role = ROLE_NONE;
        /* s_write_pending 已移除（NO_RSP 写不门控） */
        ESP_LOGI(TAG, "GATTS disconnected, restart adv/scan");
        if (s_conn_cb) s_conn_cb(false);
        if (s_active) {
            start_adv();
            start_scan();
        }
        break;

    default:
        break;
    }
}

/* ======================== GATTC（Client）回调 ======================== */
static void gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                     esp_ble_gattc_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTC_REG_EVT:
        s_gattc_if = gattc_if;
        s_gattc_reg = true;
        break;

    case ESP_GATTC_OPEN_EVT:
        /* 连接发起结果；失败通常意味着对端仍处「已连接」等异常状态（便于排查再次进入搜不到对端） */
        if (param->open.status != ESP_GATT_OK) {
            ESP_LOGW(TAG, "gattc open fail status=%d (peer may still be linked)", param->open.status);
        }
        break;

    case ESP_GATTC_CONNECT_EVT:
        /* 同 GATTS：退出后才到达的连接立刻断开，避免幽灵链路 */
        if (!s_active) {
            ESP_LOGW(TAG, "GATTC late connect while inactive, close it");
            esp_ble_gattc_close(gattc_if, param->connect.conn_id);
            break;
        }
        s_gattc_conn_id = param->connect.conn_id;
        s_link_up = true;
        s_link_is_client = true;
        memcpy(s_peer_addr, param->connect.remote_bda, 6);
        s_seed = addr_to_u32(s_own_addr) ^ addr_to_u32(param->connect.remote_bda);
        stop_adv();
        stop_scan();
        ESP_LOGI(TAG, "GATTC connected, search service seed=0x%08X", s_seed);
        {
            esp_bt_uuid_t svc = { .len = ESP_UUID_LEN_128, .uuid = { .uuid128 = {0} } };
            memcpy(svc.uuid.uuid128, PLANE_SVC_UUID128, 16);
            esp_ble_gattc_search_service(gattc_if, s_gattc_conn_id, &svc);
        }
        break;

    case ESP_GATTC_SEARCH_RES_EVT:
        s_svc_start = param->search_res.start_handle;
        s_svc_end   = param->search_res.end_handle;
        break;

    case ESP_GATTC_SEARCH_CMPL_EVT: {
        if (param->search_cmpl.status != ESP_GATT_OK) {
            ESP_LOGW(TAG, "search cmpl fail %d", param->search_cmpl.status);
            break;
        }
        uint16_t count = 0;
        esp_gatt_status_t st = esp_ble_gattc_get_attr_count(
            gattc_if, s_gattc_conn_id, ESP_GATT_DB_CHARACTERISTIC,
            s_svc_start, s_svc_end, PLANE_INVALID_HANDLE, &count);
        if (st != ESP_GATT_OK || count == 0) {
            ESP_LOGW(TAG, "no char found (count=%d)", count);
            break;
        }
        esp_gattc_char_elem_t *res = malloc(sizeof(esp_gattc_char_elem_t) * count);
        if (!res) break;
        esp_bt_uuid_t chr = { .len = ESP_UUID_LEN_128, .uuid = { .uuid128 = {0} } };
        memcpy(chr.uuid.uuid128, PLANE_CHAR_UUID128, 16);
        st = esp_ble_gattc_get_char_by_uuid(gattc_if, s_gattc_conn_id,
                                            s_svc_start, s_svc_end, chr, res, &count);
        if (st == ESP_GATT_OK && count > 0) {
            s_char_handle = res[0].char_handle;
            ESP_LOGI(TAG, "char handle=%d, register notify", s_char_handle);
            esp_ble_gattc_register_for_notify(gattc_if, s_peer_addr, s_char_handle);
        }
        free(res);
        break;
    }

    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
        if (param->reg_for_notify.status != ESP_GATT_OK) {
            ESP_LOGW(TAG, "reg notify fail %d", param->reg_for_notify.status);
            break;
        }
        uint16_t count = 0;
        esp_gatt_status_t st = esp_ble_gattc_get_attr_count(
            gattc_if, s_gattc_conn_id, ESP_GATT_DB_DESCRIPTOR,
            s_svc_start, s_svc_end, s_char_handle, &count);
        if (st != ESP_GATT_OK || count == 0) {
            ESP_LOGW(TAG, "no descr found (count=%d)", count);
            break;
        }
        esp_gattc_descr_elem_t *dr = malloc(sizeof(esp_gattc_descr_elem_t) * count);
        if (!dr) break;
        esp_bt_uuid_t cccd = { .len = ESP_UUID_LEN_16, .uuid = { .uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG } };
        st = esp_ble_gattc_get_descr_by_char_handle(gattc_if, s_gattc_conn_id,
                                                   s_char_handle, cccd, dr, &count);
        if (st == ESP_GATT_OK && count > 0 &&
            dr[0].uuid.len == ESP_UUID_LEN_16 &&
            dr[0].uuid.uuid.uuid16 == ESP_GATT_UUID_CHAR_CLIENT_CONFIG) {
            s_descr_handle = dr[0].handle;
            uint8_t cccd_val[2] = {0x01, 0x00};  /* 使能 notify */
            esp_ble_gattc_write_char_descr(gattc_if, s_gattc_conn_id, s_descr_handle,
                                          2, cccd_val, ESP_GATT_WRITE_TYPE_RSP,
                                          ESP_GATT_AUTH_REQ_NONE);
        }
        free(dr);
        break;
    }

    case ESP_GATTC_WRITE_DESCR_EVT: {
        /* notify 订阅结果。若写 CCCD 失败则重试，否则客户端收不到服务端 notify，
           表现为「首次进游戏只看到自己一架飞机」。 */
        ESP_LOGI(TAG, "CCCD write status=%d", param->write.status);
        if (param->write.status != ESP_GATT_OK && s_descr_handle != 0) {
            uint8_t cccd_val[2] = {0x01, 0x00};
            esp_ble_gattc_write_char_descr(gattc_if, s_gattc_conn_id, s_descr_handle,
                                          2, cccd_val, ESP_GATT_WRITE_TYPE_RSP,
                                          ESP_GATT_AUTH_REQ_NONE);
            break;
        }
        if (!s_connected) {
            s_connected = true;
            ESP_LOGI(TAG, "GATTC notify enabled, link ready");
            if (s_conn_cb) s_conn_cb(true);
        }
        break;
    }

    case ESP_GATTC_NOTIFY_EVT:
        if (param->notify.handle == s_char_handle && param->notify.value_len == PLANE_PKT_LEN) {
            plane_peer_state_t p;
            unpack_state(param->notify.value, &p);
            memcpy(&s_peer, &p, PLANE_PKT_LEN);
            { static uint32_t n = 0; if ((n++ % 90) == 0)
                ESP_LOGI(TAG, "CLI rx #%u x=%d y=%d alive=%d score=%d rseq=%d",
                         (unsigned)n, p.x, p.y, p.alive, p.score, p.restart_seq); }
            if (s_recv_cb) s_recv_cb(&s_peer);
        }
        break;

    case ESP_GATTC_WRITE_CHAR_EVT:
        /* s_write_pending 已移除（NO_RSP 写不门控） */
        break;

    case ESP_GATTC_DISCONNECT_EVT:
        s_connected = false;
        s_link_up = false;
        s_role = ROLE_NONE;
        /* s_write_pending 已移除（NO_RSP 写不门控） */
        ESP_LOGI(TAG, "GATTC disconnected, restart adv/scan");
        if (s_conn_cb) s_conn_cb(false);
        if (s_active) {
            start_adv();
            start_scan();
        }
        break;

    default:
        break;
    }
}

/* ======================== 对外 API ======================== */
void plane_net_init(void)
{
    if (s_inited) return;
    const uint8_t *a = esp_bt_dev_get_address();
    if (a) memcpy(s_own_addr, a, 6);

    esp_ble_gatts_register_callback(gatts_cb);
    esp_ble_gattc_register_callback(gattc_cb);
    esp_ble_gatts_app_register(GATTS_APP_ID);
    esp_ble_gattc_app_register(GATTC_APP_ID);

    s_inited = true;
    ESP_LOGI(TAG, "init own_addr=%02X:%02X:%02X:%02X:%02X:%02X",
             s_own_addr[0], s_own_addr[1], s_own_addr[2],
             s_own_addr[3], s_own_addr[4], s_own_addr[5]);
}

void plane_net_begin(plane_net_recv_cb_t recv_cb, plane_net_conn_cb_t conn_cb)
{
    if (!s_inited) plane_net_init();
    s_recv_cb = recv_cb;
    s_conn_cb = conn_cb;
    s_active = true;
    /* 防御：若上一会话残留链路（异常路径未走 stop），先断开再重新搜索 */
    if (s_link_up) {
        if (s_link_is_client) esp_ble_gattc_close(s_gattc_if, s_gattc_conn_id);
        else                  esp_ble_gatts_close(s_gatts_if, s_gatts_conn_id);
        s_link_up = false;
        ESP_LOGW(TAG, "begin: stale link closed before rescan");
    }
    s_connected = false;
    s_role = ROLE_NONE;
    s_svc_start = 0;
    s_svc_end = 0;
    memset(&s_peer, 0, sizeof(s_peer));
    s_peer.x = -1; s_peer.y = -1; s_peer.level = 1;
    /* 接管 BLE GAP 回调 */
    esp_ble_gap_register_callback(gap_cb);
    ESP_LOGI(TAG, "begin: advertising + scanning");
    start_adv();
    start_scan();
}

void plane_net_send(const plane_peer_state_t *st)
{
    if (st) memcpy(&s_local, st, PLANE_PKT_LEN);
    net_send_now();
}

void plane_net_stop(void)
{
    s_active = false;
    /* 关键：主动断开已有链路。否则对端在协议栈层仍是「已连接」，
       下次进入游戏时 Server 侧 GATTS_CONNECT_EVT 不再触发、
       Client 侧 esp_ble_gattc_open 因「已连接」失败 → 双方都搜不到对方，只能重启设备。 */
    if (s_link_up) {
        if (s_link_is_client) esp_ble_gattc_close(s_gattc_if, s_gattc_conn_id);
        else                  esp_ble_gatts_close(s_gatts_if, s_gatts_conn_id);
        s_link_up = false;
        ESP_LOGI(TAG, "stop: link closed");
    }
    stop_adv();
    stop_scan();
    s_connected = false;
    s_role = ROLE_NONE;
    /* 恢复扫描页的 GAP 回调 */
    sdgoods_ble_restore_gap_cb();
    ESP_LOGI(TAG, "stop");
}

bool plane_net_connected(void)
{
    return s_connected;
}

uint32_t plane_net_seed(void)
{
    return s_seed;
}

void plane_net_get_peer(plane_peer_state_t *out)
{
    if (out) memcpy(out, &s_peer, PLANE_PKT_LEN);
}
