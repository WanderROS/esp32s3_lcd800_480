#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_port.h"
#include <Wire.h>
#include "ESP_I2S.h"
#include "esp_check.h"
#include "es8311.h"
#include "es7210.h"
#include "ESP_SR.h"
#include "esp_partition.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

#include "pin_config.h"
#include "wifi_config.h"
#include "aliyun_asr.h"
#include "qwen_llm.h"
#include "aliyun_tts.h"

using namespace esp_panel::board;
using namespace esp_panel::drivers;

// ===== 唤醒词命令 =====
static const sr_cmd_t sr_commands[] = {};

// ===== 状态机 =====
enum VoiceState {
    STATE_IDLE,
    STATE_RECORDING,
    STATE_ASR,
    STATE_LLM,
    STATE_TTS,
};

static volatile VoiceState voice_state = STATE_IDLE;
static volatile bool wake_detected = false;

// ===== 音频配置 =====
#define EXAMPLE_SAMPLE_RATE     16000
#define EXAMPLE_VOICE_VOLUME    75
#define EXAMPLE_ES8311_MIC_GAIN (es8311_mic_gain_t)(6)
#define EXAMPLE_ES7210_MIC_GAIN GAIN_30DB
#define RECORD_TIME_SEC         6
#define RECORD_BUFFER_SIZE      (EXAMPLE_SAMPLE_RATE * RECORD_TIME_SEC)

I2SClass i2s;
static int16_t *record_buffer = NULL;

Board *board = nullptr;

// ===== UI 标签 =====
static lv_obj_t *lbl_status = NULL;
static lv_obj_t *lbl_asr    = NULL;
static lv_obj_t *lbl_reply  = NULL;

// ===== 中文字体 =====
static lv_font_t *g_font_cn_16 = nullptr;
static lv_font_t *g_font_cn_20 = nullptr;
static lv_font_t *g_font_cn_24 = nullptr;

// 需要在字体加载后更新字体的标签
static lv_obj_t *g_lbl_title       = nullptr;
static lv_obj_t *g_lbl_asr_title   = nullptr;
static lv_obj_t *g_lbl_reply_title = nullptr;
static lv_obj_t *g_lbl_hint        = nullptr;

/**
 * 从 SPIFFS 读取字体文件到 PSRAM，用 lv_binfont_create_from_buffer 加载
 */
static lv_font_t *load_font_from_spiffs(const char *path) {
    File f = SPIFFS.open(path, FILE_READ);
    if (!f) {
        Serial.printf("[FONT] Failed to open: %s\n", path);
        return nullptr;
    }
    f.seek(0, SeekEnd);
    size_t fsize = f.position();
    f.seek(0, SeekSet);
    if (fsize == 0) {
        f.close();
        return nullptr;
    }

    // 分配到 PSRAM，字体数据需要在整个生命周期内保持有效
    uint8_t *buf = (uint8_t *)heap_caps_malloc(fsize, MALLOC_CAP_SPIRAM);
    if (!buf) {
        Serial.printf("[FONT] PSRAM alloc failed for %u bytes\n", (unsigned)fsize);
        f.close();
        return nullptr;
    }

    uint32_t t0 = millis();
    size_t n = f.read(buf, fsize);
    f.close();
    Serial.printf("[FONT] %s: %u bytes, read %lu ms\n", path, (unsigned)n, millis() - t0);

    uint32_t t1 = millis();
    lv_font_t *font = lv_binfont_create_from_buffer(buf, n);
    Serial.printf("[FONT] lv_binfont_create: %lu ms, font=%p\n", millis() - t1, font);
    return font;
}

// 线程安全的 UI 更新
static void ui_set_status(const char *text) {
    if (!lvgl_port_lock(100)) return;
    if (lbl_status) lv_label_set_text(lbl_status, text);
    lvgl_port_unlock();
}
static void ui_set_asr(const char *text) {
    if (!lvgl_port_lock(100)) return;
    if (lbl_asr) lv_label_set_text(lbl_asr, text);
    lvgl_port_unlock();
}
static void ui_set_reply(const char *text) {
    if (!lvgl_port_lock(100)) return;
    if (lbl_reply) lv_label_set_text(lbl_reply, text);
    lvgl_port_unlock();
}

// ===== ES8311 初始化 =====
esp_err_t es8311_codec_init(void) {
    es8311_handle_t es_handle = es8311_create(0, ES8311_ADDRRES_0);
    ESP_RETURN_ON_FALSE(es_handle, ESP_FAIL, "ES8311", "create failed");

    const es8311_clock_config_t es_clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = EXAMPLE_SAMPLE_RATE * 256,
        .sample_frequency = EXAMPLE_SAMPLE_RATE
    };

    ESP_ERROR_CHECK(es8311_init(es_handle, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16));
    ESP_ERROR_CHECK(es8311_sample_frequency_config(es_handle, es_clk.mclk_frequency, es_clk.sample_frequency));
    ESP_ERROR_CHECK(es8311_microphone_config(es_handle, false));
    ESP_ERROR_CHECK(es8311_voice_volume_set(es_handle, EXAMPLE_VOICE_VOLUME, NULL));
    ESP_ERROR_CHECK(es8311_microphone_gain_set(es_handle, EXAMPLE_ES8311_MIC_GAIN));
    return ESP_OK;
}

// ===== 录音函数 =====
bool do_record(int16_t *out_buf, size_t samples) {
    size_t stereo_bytes = samples * 2 * sizeof(int16_t);
    int16_t *stereo = (int16_t *)heap_caps_malloc(stereo_bytes, MALLOC_CAP_SPIRAM);
    if (!stereo) {
        Serial.println("[REC] stereo buffer alloc failed");
        return false;
    }

    size_t total_read = 0;
    uint32_t deadline = millis() + (RECORD_TIME_SEC + 2) * 1000;
    while (total_read < stereo_bytes && millis() < deadline) {
        size_t n = i2s.readBytes((char *)stereo + total_read, stereo_bytes - total_read);
        total_read += n;
    }

    for (size_t i = 0; i < samples; i++) {
        out_buf[i] = stereo[i * 2];
    }
    heap_caps_free(stereo);
    Serial.printf("[REC] done, read %d bytes\n", total_read);
    return total_read > 0;
}

// ===== 音频任务 (Core 0) =====
void audio_task(void *param) {
    // I2S 初始化
    // setPins(bclk, ws, dout, din, mclk)
    //   dout = DOPIN (GPIO6, → ES8311 播放)
    //   din  = DIPIN (GPIO15, ← ES7210 录音)
    i2s.setPins(BCLKPIN, WSPIN, DOPIN, DIPIN, MCLKPIN);
    if (!i2s.begin(I2S_MODE_STD, EXAMPLE_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT,
                   I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
        Serial.println("[AUDIO] I2S init failed!");
        vTaskDelete(NULL);
    }

    // I2C: Wire 已在 setup 中初始化，直接复用

    if (es8311_codec_init() != ESP_OK) {
        Serial.println("[AUDIO] ES8311 init failed!");
        vTaskDelete(NULL);
    }

    audio_hal_codec_config_t es7210_cfg = {
        .adc_input  = AUDIO_HAL_ADC_INPUT_ALL,
        .dac_output = AUDIO_HAL_DAC_OUTPUT_ALL,
        .codec_mode = AUDIO_HAL_CODEC_MODE_ENCODE,
        .i2s_iface  = {
            .mode    = AUDIO_HAL_MODE_SLAVE,
            .fmt     = AUDIO_HAL_I2S_NORMAL,
            .samples = AUDIO_HAL_16K_SAMPLES,
            .bits    = AUDIO_HAL_BIT_LENGTH_16BITS
        }
    };
    if (es7210_adc_init(&Wire, &es7210_cfg) != ESP_OK) {
        Serial.println("[AUDIO] ES7210 init failed!");
        vTaskDelete(NULL);
    }
    es7210_mic_select((es7210_input_mics_t)(ES7210_INPUT_MIC1 | ES7210_INPUT_MIC2));
    es7210_adc_set_gain_all(EXAMPLE_ES7210_MIC_GAIN);
    es7210_adc_ctrl_state(AUDIO_HAL_CODEC_MODE_ENCODE, AUDIO_HAL_CTRL_START);

    // 使能功放 PA (NS4150) — 通过 IO 扩展器 TCA9554 (0x20) 的 P0
    {
        #define TCA9554_ADDR  0x20
        #define TCA9554_REG_OUTPUT  0x01  // Output port register
        #define TCA9554_REG_CONFIG  0x03  // Configuration register (0=output, 1=input)

        // 设置 P0 为输出
        Wire.beginTransmission(TCA9554_ADDR);
        Wire.write(TCA9554_REG_CONFIG);
        Wire.write(0xFE);  // P0=output(0), P1-P7=input(1)
        Wire.endTransmission();

        // P0 输出高电平，使能功放
        Wire.beginTransmission(TCA9554_ADDR);
        Wire.write(TCA9554_REG_OUTPUT);
        Wire.write(0x01);  // P0=HIGH
        Wire.endTransmission();

        Serial.println("[AUDIO] PA enabled via TCA9554 P0");
    }

    // 检查模型分区
    const esp_partition_t *model_part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "model");
    if (!model_part) {
        Serial.println("[AUDIO] Model partition not found!");
        vTaskDelete(NULL);
    }
    Serial.printf("[AUDIO] Model partition: 0x%x, %d bytes\n", model_part->address, model_part->size);

    vTaskDelay(pdMS_TO_TICKS(100));

    static uint32_t s_free_talk_until = 0;

    // ESP_SR 唤醒词检测
    ESP_SR.onEvent([](sr_event_t event, int command_id, int phrase_id) {
        switch (event) {
            case SR_EVENT_WAKEWORD:
                Serial.println("[SR] Wakeword detected!");
                if (voice_state == STATE_IDLE && millis() < s_free_talk_until) {
                    wake_detected = true;
                }
                break;
            case SR_EVENT_WAKEWORD_CHANNEL:
                Serial.printf("[SR] Wakeword channel %d verified!\n", command_id);
                if (voice_state == STATE_IDLE) {
                    wake_detected = true;
                }
                break;
            case SR_EVENT_TIMEOUT:
                Serial.println("[SR] Timeout");
                ESP_SR.setMode(SR_MODE_WAKEWORD);
                break;
            default: break;
        }
    });

    if (!ESP_SR.begin(i2s, sr_commands, 0, SR_CHANNELS_STEREO, SR_MODE_WAKEWORD)) {
        Serial.println("[SR] ESP_SR init failed!");
        vTaskDelete(NULL);
    }
    Serial.println("[SR] Waiting for wakeword...");
    ui_set_status("\xe7\xad\x89\xe5\xbe\x85\xe5\x94\xa4\xe9\x86\x92...");  // "等待唤醒..."

    record_buffer = (int16_t *)heap_caps_malloc(
        RECORD_BUFFER_SIZE * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!record_buffer) {
        Serial.println("[AUDIO] Record buffer alloc failed!");
        vTaskDelete(NULL);
    }

    #define FREE_TALK_TIMEOUT_MS 30000

    while (1) {
        if (wake_detected) {
            wake_detected = false;
            voice_state = STATE_RECORDING;

            ESP_SR.setMode(SR_MODE_OFF);
            vTaskDelay(pdMS_TO_TICKS(100));

            Serial.println("\n===== Recording =====");
            ui_set_status("\xe5\xbd\x95\xe9\x9f\xb3\xe4\xb8\xad...");  // "录音中..."
            ui_set_asr("");
            ui_set_reply("");

            bool rec_ok = do_record(record_buffer, RECORD_BUFFER_SIZE);
            if (!rec_ok) {
                ui_set_status("\xe5\xbd\x95\xe9\x9f\xb3\xe5\xa4\xb1\xe8\xb4\xa5");  // "录音失败"
                s_free_talk_until = 0;
                voice_state = STATE_IDLE;
                ESP_SR.setMode(SR_MODE_WAKEWORD);
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }

            // ASR
            voice_state = STATE_ASR;
            ui_set_status("\xe8\xaf\x86\xe5\x88\xab\xe4\xb8\xad...");  // "识别中..."
            String asr_text;
            bool asr_ok = aliyun_asr_recognize(record_buffer, RECORD_BUFFER_SIZE, asr_text);
            if (!asr_ok || asr_text.isEmpty()) {
                s_free_talk_until = 0;
                ui_set_status("\xe7\xad\x89\xe5\xbe\x85\xe5\x94\xa4\xe9\x86\x92...");  // "等待唤醒..."
                voice_state = STATE_IDLE;
                ESP_SR.setMode(SR_MODE_WAKEWORD);
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            Serial.printf("[ASR] %s\n", asr_text.c_str());
            ui_set_asr(asr_text.c_str());

            // LLM
            voice_state = STATE_LLM;
            ui_set_status("\xe6\x80\x9d\xe8\x80\x83\xe4\xb8\xad...");  // "思考中..."
            String llm_reply;
            bool llm_ok = qwen_chat(asr_text, llm_reply);
            if (!llm_ok || llm_reply.isEmpty()) {
                ui_set_status("\xe7\xbd\x91\xe7\xbb\x9c\xe9\x94\x99\xe8\xaf\xaf");  // "网络错误"
                aliyun_tts_speak("\xe6\x8a\xb1\xe6\xad\x89\xef\xbc\x8c\xe7\xbd\x91\xe7\xbb\x9c\xe5\x87\xba\xe7\x8e\xb0\xe9\x97\xae\xe9\xa2\x98\xe3\x80\x82");  // "抱歉，网络出现问题。"
                s_free_talk_until = 0;
                voice_state = STATE_IDLE;
                ESP_SR.setMode(SR_MODE_WAKEWORD);
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
            Serial.printf("[LLM] %s\n", llm_reply.c_str());
            ui_set_reply(llm_reply.c_str());

            // TTS
            voice_state = STATE_TTS;
            ui_set_status("\xe6\x92\xad\xe6\x94\xbe\xe4\xb8\xad...");  // "播放中..."
            aliyun_tts_speak(llm_reply);

            // 免唤醒窗口
            s_free_talk_until = millis() + FREE_TALK_TIMEOUT_MS;
            ui_set_status("\xe7\xbb\xa7\xe7\xbb\xad\xe8\xaf\xb4\xe8\xaf\x9d (30s)...");  // "继续说话 (30s)..."
            voice_state = STATE_IDLE;
            ESP_SR.setMode(SR_MODE_WAKEWORD);
            vTaskDelay(pdMS_TO_TICKS(800));
            wake_detected = true;
        }

        if (s_free_talk_until > 0 && voice_state == STATE_IDLE) {
            if (millis() >= s_free_talk_until) {
                s_free_talk_until = 0;
                ui_set_status("\xe7\xad\x89\xe5\xbe\x85\xe5\x94\xa4\xe9\x86\x92...");  // "等待唤醒..."
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ===== 创建语音助手 UI（中文） =====
static void create_voice_ui(void) {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);

    const lv_font_t *f24 = g_font_cn_24 ? g_font_cn_24 : &lv_font_montserrat_24;
    const lv_font_t *f16 = g_font_cn_16 ? g_font_cn_16 : &lv_font_montserrat_16;
    const lv_font_t *f14 = &lv_font_montserrat_14;

    // 标题
    g_lbl_title = lv_label_create(scr);
    lv_label_set_text(g_lbl_title, "AI \xe8\xaf\xad\xe9\x9f\xb3\xe5\x8a\xa9\xe6\x89\x8b");  // "AI 语音助手"
    lv_obj_set_style_text_color(g_lbl_title, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_text_font(g_lbl_title, f24, 0);
    lv_obj_align(g_lbl_title, LV_ALIGN_TOP_MID, 0, 20);

    // 状态
    lbl_status = lv_label_create(scr);
    lv_label_set_text(lbl_status, "\xe7\xad\x89\xe5\xbe\x85\xe5\x94\xa4\xe9\x86\x92...");  // "等待唤醒..."
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(0xffd700), 0);
    lv_obj_set_style_text_font(lbl_status, f16, 0);
    lv_obj_align(lbl_status, LV_ALIGN_TOP_MID, 0, 60);

    // 分隔线
    lv_obj_t *line = lv_obj_create(scr);
    lv_obj_set_size(line, 740, 2);
    lv_obj_set_style_bg_color(line, lv_color_hex(0x444466), 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 100);

    // ASR 标题
    g_lbl_asr_title = lv_label_create(scr);
    lv_label_set_text(g_lbl_asr_title, "\xe4\xbd\xa0\xe8\xaf\xb4\xef\xbc\x9a");  // "你说："
    lv_obj_set_style_text_color(g_lbl_asr_title, lv_color_hex(0x88aaff), 0);
    lv_obj_set_style_text_font(g_lbl_asr_title, f16, 0);
    lv_obj_align(g_lbl_asr_title, LV_ALIGN_TOP_LEFT, 30, 115);

    lbl_asr = lv_label_create(scr);
    lv_label_set_text(lbl_asr, "");
    lv_label_set_long_mode(lbl_asr, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl_asr, 740);
    lv_obj_set_style_text_color(lbl_asr, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(lbl_asr, f16, 0);
    lv_obj_align(lbl_asr, LV_ALIGN_TOP_LEFT, 30, 145);

    // AI 回复标题
    g_lbl_reply_title = lv_label_create(scr);
    lv_label_set_text(g_lbl_reply_title, "AI\xef\xbc\x9a");  // "AI："
    lv_obj_set_style_text_color(g_lbl_reply_title, lv_color_hex(0x88ffaa), 0);
    lv_obj_set_style_text_font(g_lbl_reply_title, f16, 0);
    lv_obj_align(g_lbl_reply_title, LV_ALIGN_TOP_LEFT, 30, 230);

    lbl_reply = lv_label_create(scr);
    lv_label_set_text(lbl_reply, "");
    lv_label_set_long_mode(lbl_reply, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl_reply, 740);
    lv_obj_set_style_text_color(lbl_reply, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(lbl_reply, f16, 0);
    lv_obj_align(lbl_reply, LV_ALIGN_TOP_LEFT, 30, 260);

    // 底部提示
    g_lbl_hint = lv_label_create(scr);
    lv_label_set_text(g_lbl_hint,
        "\xe8\xaf\xb4 '\xe5\xb0\x8f\xe7\x88\xb1\xe5\x90\x8c\xe5\xad\xa6' \xe5\x94\xa4\xe9\x86\x92");  // "说 '小爱同学' 唤醒"
    lv_obj_set_style_text_color(g_lbl_hint, lv_color_hex(0x666688), 0);
    lv_obj_set_style_text_font(g_lbl_hint, f16, 0);
    lv_obj_align(g_lbl_hint, LV_ALIGN_BOTTOM_MID, 0, -20);
}

// ===== setup =====
void setup() {
    Serial.begin(115200);
    delay(2000);

    // SPIFFS 初始化（字体文件）
    if (!SPIFFS.begin(true)) {
        Serial.println("[SPIFFS] Init failed!");
    } else {
        Serial.println("[SPIFFS] Init OK");
    }

    // 先用 Wire 初始化 I2C bus 0（SDA=8, SCL=18）
    // 触摸和音频 codec (ES8311/ES7210) 共用此总线
    // Board 的触摸 SKIP_INIT_HOST=1，不会重复安装驱动
    Wire.begin(IIC_SDA, IIC_SCL);

    // 初始化显示板（触摸复用 Wire 已初始化的 I2C host）
    board = new Board();
    if (!board->begin()) {
        Serial.println("Board init failed!");
        return;
    }

    auto backlight = board->getBacklight();
    if (backlight) backlight->on();

    // LVGL 初始化
    lvgl_port_init(board->getLCD(), board->getTouch());

    // 加载中文字体（从 SPIFFS → PSRAM → LVGL binfont）
    Serial.println("[FONT] Loading Chinese fonts...");
    uint32_t ft0 = millis();
    lvgl_port_lock(-1);
    g_font_cn_24 = load_font_from_spiffs("/fonts/cn24.bin");
    g_font_cn_20 = load_font_from_spiffs("/fonts/cn20.bin");
    g_font_cn_16 = load_font_from_spiffs("/fonts/cn16.bin");
    lvgl_port_unlock();
    Serial.printf("[FONT] All loaded in %lu ms\n", millis() - ft0);

    // 创建语音助手 UI（使用已加载的中文字体）
    lvgl_port_lock(-1);
    create_voice_ui();
    lvgl_port_unlock();

    // 连接 WiFi
    Serial.printf("[WiFi] Connecting to %s ...\n", WIFI_SSID);
    ui_set_status("\xe8\xbf\x9e\xe6\x8e\xa5 WiFi...");  // "连接 WiFi..."
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t wifi_timeout = millis() + 15000;
    while (WiFi.status() != WL_CONNECTED && millis() < wifi_timeout) {
        delay(200);
        Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] Connected, IP: %s\n", WiFi.localIP().toString().c_str());
        ui_set_status("WiFi \xe5\xb7\xb2\xe8\xbf\x9e\xe6\x8e\xa5");  // "WiFi 已连接"
        // 禁用 WiFi 省电，防止 PHY 释放后 SR 占满内存导致无法重新分配
        WiFi.setSleep(false);
    } else {
        Serial.println("\n[WiFi] Connection failed!");
        ui_set_status("WiFi \xe8\xbf\x9e\xe6\x8e\xa5\xe5\xa4\xb1\xe8\xb4\xa5!");  // "WiFi 连接失败!"
    }

    Serial.printf("[MEM] Heap: %d, PSRAM: %d\n", ESP.getFreeHeap(), ESP.getFreePsram());

    // 启动音频任务 (Core 0，因为 LVGL 任务在 Core 1)
    vTaskDelay(pdMS_TO_TICKS(1000));
    xTaskCreatePinnedToCore(audio_task, "audio_task", 12288, NULL, 5, NULL, 0);

    Serial.println("[SETUP] Init complete");
}

// ===== loop =====
void loop() {
    delay(1000);
}
