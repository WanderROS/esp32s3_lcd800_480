#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "lvgl_port.h"

using namespace esp_panel::drivers;

#define LVGL_PORT_TICK_PERIOD_MS        2
#define LVGL_PORT_BUFFER_HEIGHT         20
#define LVGL_PORT_TASK_STACK_SIZE       (6 * 1024)
#define LVGL_PORT_TASK_PRIORITY         2
#define LVGL_PORT_TASK_MAX_DELAY_MS     500
#define LVGL_PORT_TASK_MIN_DELAY_MS     2

static SemaphoreHandle_t lvgl_mux = nullptr;
static TaskHandle_t lvgl_task_handle = nullptr;
static esp_timer_handle_t lvgl_tick_timer = nullptr;

static void tick_increment(void *arg)
{
    lv_tick_inc(LVGL_PORT_TICK_PERIOD_MS);
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    LCD *lcd = (LCD *)lv_display_get_user_data(disp);
    int x1 = area->x1;
    int y1 = area->y1;
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;

    lcd->drawBitmap(x1, y1, w, h, px_map);

    // RGB LCD 不需要等 DMA 完成
    if (lcd->getBus()->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        lv_display_flush_ready(disp);
    }
}

static IRAM_ATTR bool on_draw_finish(void *user_data)
{
    lv_display_t *disp = (lv_display_t *)user_data;
    lv_display_flush_ready(disp);
    return false;
}

static void touchpad_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    Touch *tp = (Touch *)lv_indev_get_user_data(indev);
    TouchPoint point;
    data->state = LV_INDEV_STATE_RELEASED;

    int n = tp->readPoints(&point, 1, 0);
    if (n > 0) {
        data->point.x = point.x;
        data->point.y = point.y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
}

static void lvgl_port_task(void *arg)
{
    uint32_t delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
    while (1) {
        if (lvgl_port_lock(-1)) {
            delay_ms = lv_timer_handler();
            lvgl_port_unlock();
        }
        if (delay_ms > LVGL_PORT_TASK_MAX_DELAY_MS) delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
        else if (delay_ms < LVGL_PORT_TASK_MIN_DELAY_MS) delay_ms = LVGL_PORT_TASK_MIN_DELAY_MS;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

bool lvgl_port_init(LCD *lcd, Touch *tp)
{
    lv_init();

    // Tick
    const esp_timer_create_args_t tick_args = {
        .callback = &tick_increment,
        .name = "lvgl_tick"
    };
    esp_timer_create(&tick_args, &lvgl_tick_timer);
    esp_timer_start_periodic(lvgl_tick_timer, LVGL_PORT_TICK_PERIOD_MS * 1000);

    // Display
    int w = lcd->getFrameWidth();
    int h = lcd->getFrameHeight();
    lv_display_t *disp = lv_display_create(w, h);
    lv_display_set_user_data(disp, (void *)lcd);
    lv_display_set_flush_cb(disp, flush_cb);

    uint32_t buf_size = w * LVGL_PORT_BUFFER_HEIGHT * sizeof(lv_color16_t);
    void *buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    void *buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    // 非 RGB LCD 需要 draw finish 回调
    if (lcd->getBus()->getBasicAttributes().type != ESP_PANEL_BUS_TYPE_RGB) {
        lcd->attachDrawBitmapFinishCallback(on_draw_finish, (void *)disp);
    }

    // Touch
    if (tp) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touchpad_read_cb);
        lv_indev_set_user_data(indev, (void *)tp);
        lv_indev_set_display(indev, disp);
    }

    // Mutex + Task
    lvgl_mux = xSemaphoreCreateRecursiveMutex();
    xTaskCreatePinnedToCore(lvgl_port_task, "lvgl", LVGL_PORT_TASK_STACK_SIZE, NULL,
                            LVGL_PORT_TASK_PRIORITY, &lvgl_task_handle, 1);

    return true;
}

bool lvgl_port_lock(int timeout_ms)
{
    if (!lvgl_mux) return false;
    TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_mux, ticks) == pdTRUE;
}

bool lvgl_port_unlock(void)
{
    if (!lvgl_mux) return false;
    xSemaphoreGiveRecursive(lvgl_mux);
    return true;
}
