#pragma once
/**
 * BLE Provisioning 蓝牙配网模块
 *
 * 使用 ESP32 Arduino 框架自带的 WiFiProv 库，通过 BLE 接收 WiFi 凭据。
 * 手机端使用 Espressif 官方 "ESP BLE Provisioning" App:
 *   - Android: https://play.google.com/store/apps/details?id=com.espressif.provble
 *   - iOS:     https://apps.apple.com/app/esp-ble-provisioning/id1473590141
 *
 * 流程:
 *   1. 调用 ble_prov_start() 启动配网
 *   2. 内部会注册 WiFi 事件回调，自动处理配网状态
 *   3. 调用 ble_prov_wait() 阻塞等待直到获取 IP 或超时
 *   4. 配网成功后凭据自动保存到 NVS，下次重启自动连接
 */

#include <WiFi.h>
#include <WiFiProv.h>
#include <lvgl.h>

// ===== BLE 配网参数 =====
#define BLE_PROV_POP          "12345678"       // Proof of Possession (配对密码)
#define BLE_PROV_SERVICE_NAME "PROV_AIVA"      // BLE 设备名 (Espressif App 要求 PROV_ 前缀)
#define BLE_PROV_TIMEOUT_MS   120000           // 配网超时 120 秒

// 配网状态标志 (由事件回调设置)
static volatile bool _prov_wifi_connected = false;
static volatile bool _prov_failed = false;
static volatile bool _prov_ended = false;

// QR 码弹窗对象
static lv_obj_t *_prov_qr_popup = NULL;

/**
 * WiFi / Provisioning 事件回调
 * 注意: 此函数在独立的 FreeRTOS 任务中被调用
 */
static void _ble_prov_event_cb(arduino_event_t *event) {
    switch (event->event_id) {
        case ARDUINO_EVENT_PROV_START:
            Serial.println("[PROV] BLE Provisioning started");
            Serial.println("[PROV] Use 'ESP BLE Provisioning' App to send WiFi credentials");
            break;

        case ARDUINO_EVENT_PROV_CRED_RECV:
            Serial.printf("[PROV] Received SSID: %s\n",
                          (const char *)event->event_info.prov_cred_recv.ssid);
            break;

        case ARDUINO_EVENT_PROV_CRED_FAIL:
            Serial.println("[PROV] Provisioning credential failed!");
            if (event->event_info.prov_fail_reason == NETWORK_PROV_WIFI_STA_AUTH_ERROR) {
                Serial.println("[PROV] WiFi password incorrect");
            } else {
                Serial.println("[PROV] WiFi AP not found");
            }
            _prov_failed = true;
            break;

        case ARDUINO_EVENT_PROV_CRED_SUCCESS:
            Serial.println("[PROV] Provisioning successful, credentials saved to NVS");
            break;

        case ARDUINO_EVENT_PROV_END:
            Serial.println("[PROV] Provisioning ended");
            _prov_ended = true;
            break;

        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.printf("[PROV] WiFi connected, IP: %s\n",
                          IPAddress(event->event_info.got_ip.ip_info.ip.addr).toString().c_str());
            _prov_wifi_connected = true;
            break;

        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.println("[PROV] WiFi disconnected");
            _prov_wifi_connected = false;
            break;

        default:
            break;
    }
}

/**
 * 启动 BLE 配网服务
 * @param reset_prov  true = 清除之前保存的凭据，强制重新配网
 */
static void ble_prov_start(bool reset_prov = false) {
    _prov_wifi_connected = false;
    _prov_failed = false;
    _prov_ended = false;

    WiFi.onEvent(_ble_prov_event_cb);

    // BLE 配网 UUID
    uint8_t uuid[16] = {0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
                        0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02};

    Serial.println("[PROV] Starting BLE Provisioning...");
    Serial.printf("[PROV] Device name: %s, POP: %s\n", BLE_PROV_SERVICE_NAME, BLE_PROV_POP);

    WiFiProv.beginProvision(
        NETWORK_PROV_SCHEME_BLE,
        NETWORK_PROV_SCHEME_HANDLER_FREE_BLE,
        NETWORK_PROV_SECURITY_1,
        BLE_PROV_POP,
        BLE_PROV_SERVICE_NAME,
        NULL,       // service_key (SoftAP 用, BLE 不需要)
        uuid,
        reset_prov
    );

    WiFiProv.printQR(BLE_PROV_SERVICE_NAME, BLE_PROV_POP, "ble");
}

/**
 * 阻塞等待 WiFi 连接成功或超时
 * @return true = WiFi 已连接, false = 超时
 */
static bool ble_prov_wait(uint32_t timeout_ms = BLE_PROV_TIMEOUT_MS) {
    uint32_t start = millis();
    while (!_prov_wifi_connected && (millis() - start < timeout_ms)) {
        delay(200);
    }
    return _prov_wifi_connected;
}

/**
 * 检查 WiFi 是否已通过配网连接
 */
static bool ble_prov_is_connected() {
    return _prov_wifi_connected;
}

/**
 * 等待 BLE 配网流程彻底结束 (PROV_END)，确保 BLE 栈内存已释放
 * 在启动 ESP_SR 等大内存消费者之前调用
 */
static void ble_prov_wait_done(uint32_t timeout_ms = 10000) {
    uint32_t start = millis();
    while (!_prov_ended && (millis() - start < timeout_ms)) {
        delay(100);
    }
    if (_prov_ended) {
        Serial.println("[PROV] BLE resources released");
    } else {
        Serial.println("[PROV] Warning: PROV_END not received, BLE may still hold memory");
    }
    // 额外等待让 BLE 栈完成清理
    delay(500);
    Serial.printf("[PROV] Post-release heap: %d\n", ESP.getFreeHeap());
}

/**
 * 清除 NVS 中保存的配网凭据并重启设备
 * 重启后将重新进入 BLE 配网模式
 */
static void ble_prov_reset() {
    Serial.println("[PROV] Erasing provisioned credentials and restarting...");
    WiFi.disconnect(true, true);  // disconnect + erase NVS credentials
    delay(500);
    ESP.restart();
}

// lvgl_port_lock / unlock 声明
#include "lvgl_port.h"

/**
 * 在屏幕上显示 BLE 配网二维码弹窗
 * @param cn_font  中文字体指针 (如 g_font_cn_16)，为 NULL 时回退到内置字体
 */
static void ble_prov_show_qr(const lv_font_t *cn_font = NULL) {
    // 构造与日志中一致的 QR payload
    char qr_payload[256];
    snprintf(qr_payload, sizeof(qr_payload),
             "{\"ver\":\"v1\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"ble\"}",
             BLE_PROV_SERVICE_NAME, BLE_PROV_POP);

    if (!lvgl_port_lock(200)) return;

    if (_prov_qr_popup) {
        lv_obj_delete(_prov_qr_popup);
        _prov_qr_popup = NULL;
    }

    lv_obj_t *scr = lv_screen_active();

    // 半透明遮罩 + 弹窗容器
    _prov_qr_popup = lv_obj_create(scr);
    lv_obj_set_size(_prov_qr_popup, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(_prov_qr_popup, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(_prov_qr_popup, LV_OPA_90, 0);
    lv_obj_set_style_border_width(_prov_qr_popup, 0, 0);
    lv_obj_set_style_radius(_prov_qr_popup, 0, 0);
    lv_obj_set_style_pad_all(_prov_qr_popup, 0, 0);
    lv_obj_set_flex_flow(_prov_qr_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_prov_qr_popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(_prov_qr_popup, 12, 0);

    const lv_font_t *f16 = cn_font ? cn_font : &lv_font_montserrat_16;

    // 标题
    lv_obj_t *title = lv_label_create(_prov_qr_popup);
    lv_label_set_text(title, "\xe8\x93\x9d\xe7\x89\x99\xe9\x85\x8d\xe7\xbd\x91");  // "蓝牙配网"
    lv_obj_set_style_text_color(title, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_text_font(title, f16, 0);

    // QR 码
    lv_obj_t *qr = lv_qrcode_create(_prov_qr_popup);
    lv_qrcode_set_size(qr, 200);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_update(qr, qr_payload, strlen(qr_payload));

    // 提示文字
    lv_obj_t *hint1 = lv_label_create(_prov_qr_popup);
    lv_label_set_text(hint1,
        "\xe8\xaf\xb7\xe7\x94\xa8 ESP BLE Provisioning App \xe6\x89\xab\xe7\xa0\x81");  // "请用 ESP BLE Provisioning App 扫码"
    lv_obj_set_style_text_color(hint1, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_text_font(hint1, f16, 0);

    lv_obj_t *hint2 = lv_label_create(_prov_qr_popup);
    lv_label_set_text_fmt(hint2, "PIN: %s", BLE_PROV_POP);
    lv_obj_set_style_text_color(hint2, lv_color_hex(0xffd700), 0);
    lv_obj_set_style_text_font(hint2, f16, 0);

    lvgl_port_unlock();
    Serial.println("[PROV] QR code displayed on screen");
}

/**
 * 隐藏配网二维码弹窗
 */
static void ble_prov_hide_qr() {
    if (!lvgl_port_lock(200)) return;
    if (_prov_qr_popup) {
        lv_obj_delete(_prov_qr_popup);
        _prov_qr_popup = NULL;
    }
    lvgl_port_unlock();
}
