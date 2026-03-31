// Target Hardware: VIEWE SMARTRING (ESP32-S3 + SH8601 AMOLED)
// Manufacturer Source: https://github.com/VIEWESMART/VIEWE-SMARTRING

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"

#define TARGET_NAME "vLinker MC-IOS"
#define LCD_HOST SPI2_HOST
#define PIN_NUM_CS 7
#define PIN_NUM_PCLK 13
#define PIN_NUM_DATA0 12
#define PIN_NUM_DATA1 8
#define PIN_NUM_DATA2 14
#define PIN_NUM_DATA3 9
#define PIN_NUM_RST 11
#define PIN_NUM_BK_LIGHT 40
#define LCD_H_RES 466
#define LCD_V_RES 466
#define LCD_BIT_PER_PIXEL 16
#define LVGL_BUF_SIZE (LCD_H_RES * 20)


typedef enum {
    STATE_DISCONNECTED,
    STATE_CONNECTED,
    STATE_READY
} app_state_t;

static app_state_t state = STATE_DISCONNECTED;
static uint16_t conn_handle;
static uint16_t obd_char_handle = 0;
static char last_rx_str[32] = "---";

static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xFE, (uint8_t []){0x00}, 0, 0},   
    {0xC4, (uint8_t []){0x80}, 1, 0},
    {0x3A, (uint8_t []){0x55}, 1, 0},
    {0x35, (uint8_t []){0x00}, 0, 10},
    {0x53, (uint8_t []){0x20}, 1, 10},    
    {0x51, (uint8_t []){0xFF}, 1, 10},
    {0x63, (uint8_t []){0xFF}, 1, 10},
    {0x2A, (uint8_t []){0x00,0x06,0x01,0xDD}, 4, 0},
    {0x2B, (uint8_t []){0x00,0x00,0x01,0xD1}, 4, 0},
    {0x11, (uint8_t []){0x00}, 0, 60},
    {0x29, (uint8_t []){0x00}, 0, 0},
};

static void ble_write_obd(const char *cmd) {
    if (obd_char_handle == 0 || state != STATE_READY) return;
    uint8_t data[32];
    int len = snprintf((char*)data, sizeof(data), "%s\r", cmd);
    ble_gattc_write_flat(conn_handle, obd_char_handle, data, len, NULL, NULL);
}

static void lv_tick_task(void *arg) { lv_tick_inc(1); }

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map) {
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t) drv->user_data;
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, (void *)color_map);
    lv_disp_flush_ready(drv);
}

static int gap_cb(struct ble_gap_event *event, void *arg);

static void start_scan(void) {
    struct ble_gap_disc_params disc_params = {0};
    disc_params.filter_duplicates = 1;
    ble_gap_disc(BLE_OWN_ADDR_PUBLIC, 15000, &disc_params, gap_cb, NULL);
}

static int on_sub(uint16_t conn_id, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg) {
    state = STATE_READY;
    ble_write_obd("ATZ");
    return 0;
}

static int chr_cb(uint16_t conn_id, const struct ble_gatt_error *error, const struct ble_gatt_chr *chr, void *arg) {
    if (error->status != 0) {
        if (error->status == 14 && obd_char_handle != 0) {
            uint16_t val = 0x0001;
            return ble_gattc_write_flat(conn_id, obd_char_handle + 1, &val, 2, on_sub, NULL);
        }
        return 0;
    }
    if (chr != NULL && (chr->properties & BLE_GATT_CHR_PROP_NOTIFY)) {
        obd_char_handle = chr->val_handle;
    }
    return 0;
}

static int gap_cb(struct ble_gap_event *event, void *arg) {
    struct ble_hs_adv_fields fields;
    switch (event->type) {
        case BLE_GAP_EVENT_DISC:
            ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
            if (fields.name_len > 0 && strncmp((char *)fields.name, TARGET_NAME, fields.name_len) == 0) {
                ble_gap_disc_cancel();
                struct ble_gap_conn_params conn_p = {.scan_itvl=128,.scan_window=64,.itvl_min=24,.itvl_max=40,.supervision_timeout=256};
                ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &event->disc.addr, 30000, &conn_p, gap_cb, NULL);
            }
            break;
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                conn_handle = event->connect.conn_handle;
                state = STATE_CONNECTED;
                ble_gattc_disc_all_chrs(conn_handle, 1, 0xFFFF, chr_cb, NULL);
            } else { start_scan(); }
            break;
        case BLE_GAP_EVENT_NOTIFY_RX: {
            char rx_temp[64] = {0};
            os_mbuf_copydata(event->notify_rx.om, 0, OS_MBUF_PKTLEN(event->notify_rx.om), rx_temp);
            char *p = strstr(rx_temp, "41 0C");
            if (p) {
                unsigned int a, b;
                if (sscanf(p + 5, "%x %x", &a, &b) == 2) {
                    memset(last_rx_str, 0, sizeof(last_rx_str)); 
                    snprintf(last_rx_str, sizeof(last_rx_str), "%d", (a * 256 + b) / 4); 
                }
            }
            break;
        }
        case BLE_GAP_EVENT_DISCONNECT:
            state = STATE_DISCONNECTED;
            start_scan();
            break;
    }
    return 0;
}

static void nimble_host_task(void *param) { nimble_port_run(); }

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_flash_init();
    }

    gpio_config_t bk = {.mode = GPIO_MODE_OUTPUT, .pin_bit_mask = 1ULL << PIN_NUM_BK_LIGHT};
    gpio_config(&bk);
    gpio_set_level(PIN_NUM_BK_LIGHT, 1);

    spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(PIN_NUM_PCLK, PIN_NUM_DATA0, PIN_NUM_DATA1, PIN_NUM_DATA2, PIN_NUM_DATA3, LCD_H_RES * LCD_V_RES * 2);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(PIN_NUM_CS, NULL, NULL);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_handle_t panel = NULL;
    sh8601_vendor_config_t vendor_config = {.init_cmds = lcd_init_cmds, .init_cmds_size = sizeof(lcd_init_cmds)/sizeof(sh8601_lcd_init_cmd_t), .flags = {.use_qspi_interface=1}};
    esp_lcd_panel_dev_config_t panel_config = {.reset_gpio_num = PIN_NUM_RST, .bits_per_pixel = LCD_BIT_PER_PIXEL, .vendor_config = &vendor_config};
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io_handle, &panel_config, &panel));
    esp_lcd_panel_reset(panel);
    esp_lcd_panel_init(panel);
    esp_lcd_panel_set_gap(panel, 6, 0);
    esp_lcd_panel_disp_on_off(panel, true);

    lv_init();
    lv_color_t *buf1 = heap_caps_malloc(LVGL_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    lv_color_t *buf2 = heap_caps_malloc(LVGL_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LVGL_BUF_SIZE);
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES; disp_drv.ver_res = LCD_V_RES;
    disp_drv.flush_cb = lvgl_flush_cb; disp_drv.draw_buf = &draw_buf; disp_drv.user_data = panel;
    lv_disp_drv_register(&disp_drv);

    const esp_timer_create_args_t t_args = {.callback = lv_tick_task, .name = "lv_tick"};
    esp_timer_handle_t timer;
    esp_timer_create(&t_args, &timer);
    esp_timer_start_periodic(timer, 1000);

    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
    
    lv_obj_t *lbl_status = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_color(lbl_status, lv_color_white(), 0);
    lv_obj_align(lbl_status, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_18, 0);
	lv_obj_set_style_bg_opa(lbl_status, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(lbl_status, lv_color_black(), 0);

    lv_obj_t *lbl_data = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_color(lbl_data, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_data, &lv_font_montserrat_48, 0);
    lv_obj_center(lbl_data);
	lv_obj_set_style_bg_opa(lbl_data, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(lbl_data, lv_color_black(), 0);
	lv_obj_set_width(lbl_data, 300);
	lv_obj_set_style_text_align(lbl_data, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *lbl_unit = lv_label_create(lv_scr_act());
	lv_obj_set_style_text_color(lbl_unit, lv_color_hex(0x00FF00), 0);
    lv_label_set_text(lbl_unit, "RPM");
    lv_obj_align_to(lbl_unit, lbl_data, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    nimble_port_init();
    ble_svc_gap_device_name_set("ESP32_OBD");
    ble_hs_cfg.sync_cb = start_scan;
    xTaskCreatePinnedToCore(nimble_host_task, "nimble_host", 4096, NULL, 10, NULL, 0); 

    uint32_t last_q = 0;
    while(1) {
        lv_timer_handler();
        if (state == STATE_READY) {
            lv_label_set_text(lbl_status, "CONNECTED");
            lv_label_set_text(lbl_data, last_rx_str);
        } else {
            lv_label_set_text(lbl_status, "SCANNING...");
            lv_label_set_text(lbl_data, "---");
        }
        if (state == STATE_READY && (esp_timer_get_time() / 1000 - last_q > 1000)) {
            ble_write_obd("010C");
            last_q = esp_timer_get_time() / 1000;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}