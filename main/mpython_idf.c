#include <stdio.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_board_manager_includes.h"
#include "esp_lvgl_port.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_types.h"
#include "lvgl.h"
#include "esp_board_manager.h"
#include "esp_board_manager_err.h"

#define TAG "LVGL_EXAMPLE"

#define LVGL_TICK_PERIOD_MS      5
#define LVGL_TASK_MAX_SLEEP_MS   500
#define LVGL_TASK_MIN_SLEEP_MS   5
#define LVGL_TASK_STACK_SIZE     (10 * 1024)
#define LVGL_TASK_PRIORITY       5
#define LVGL_FALLBACK_BUF_LINES  20

static void                     *lcd_handle   = NULL;
static esp_lcd_panel_handle_t    panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle    = NULL;
static lv_display_t *disp        = NULL;
static periph_ledc_handle_t *ledc_handle = NULL;
static void                     *touch_handle = NULL;
static esp_lcd_touch_handle_t    touch_panel_handle = NULL;

static lv_obj_t *label_title = NULL;
static lv_obj_t *label_info = NULL;
static lv_obj_t *slider = NULL;
static lv_obj_t *label_slider = NULL;

static esp_err_t lcd_lvgl_port_init(void)
{
    ESP_LOGI(TAG, "Initializing LVGL port...");
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = LVGL_TASK_PRIORITY,
        .task_stack = LVGL_TASK_STACK_SIZE,
        .task_max_sleep_ms = LVGL_TASK_MAX_SLEEP_MS,
        .timer_period_ms = LVGL_TICK_PERIOD_MS,
    };

    esp_err_t ret = lvgl_port_init(&lvgl_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LVGL port: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        static uint32_t click_count = 0;
        click_count++;
        char buf[64];
        snprintf(buf, sizeof(buf), "Button clicked %" PRIu32 " times!", click_count);
        lv_label_set_text(label_info, buf);
        ESP_LOGI(TAG, "Button clicked: %" PRIu32, click_count);
    }
}

static void slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t value = lv_slider_get_value(slider);
    char buf[32];
    snprintf(buf, sizeof(buf), "Brightness: %" PRId32 "%%", value);
    lv_label_set_text(label_slider, buf);
}

static void create_lvgl_ui(void)
{
    ESP_LOGI(TAG, "Creating LVGL UI...");

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a2e), LV_PART_MAIN);

    label_title = lv_label_create(screen);
    lv_label_set_text(label_title, "Welcome to LVGL!");
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 50);

    label_info = lv_label_create(screen);
    lv_label_set_text(label_info, "Click the button below!");
    lv_obj_set_style_text_font(label_info, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_info, lv_color_hex(0xcccccc), LV_PART_MAIN);
    lv_obj_align(label_info, LV_ALIGN_TOP_MID, 0, 120);

    lv_obj_t *btn = lv_button_create(screen);
    lv_obj_set_size(btn, 200, 60);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, -50);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Click Me!");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(btn_label);

    slider = lv_slider_create(screen);
    lv_obj_set_size(slider, 300, 30);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, 50, LV_ANIM_ON);
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, 50);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    label_slider = lv_label_create(screen);
    lv_label_set_text(label_slider, "Brightness: 50%");
    lv_obj_set_style_text_font(label_slider, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_slider, lv_color_hex(0xcccccc), LV_PART_MAIN);
    lv_obj_align(label_slider, LV_ALIGN_CENTER, 0, 100);

    lv_obj_t *arc = lv_arc_create(screen);
    lv_obj_set_size(arc, 150, 150);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 75);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_obj_align(arc, LV_ALIGN_BOTTOM_RIGHT, -50, -50);

    lv_obj_t *arc_label = lv_label_create(arc);
    lv_label_set_text(arc_label, "75%");
    lv_obj_set_style_text_font(arc_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(arc_label);

    ESP_LOGI(TAG, "LVGL UI created successfully");
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI(TAG, "Starting LVGL example with Board Manager...");

    ESP_LOGI(TAG, "Initializing board manager...");
    int ret = esp_board_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize board manager");
        return;
    }

    ret = lcd_lvgl_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LVGL port initialization failed");
        return;
    }

    ESP_LOGI(TAG, "Initializing LCD display using Board Manager...");
    ret = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, &lcd_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LCD device handle: %s", esp_err_to_name(ret));
        return;
    }

    if (lcd_handle) {
        dev_display_lcd_handles_t *lcd_handles = (dev_display_lcd_handles_t *)lcd_handle;
        panel_handle = lcd_handles->panel_handle;
        io_handle = lcd_handles->io_handle;

        dev_display_lcd_config_t *lcd_cfg = NULL;
        ret = esp_board_manager_get_device_config("display_lcd", (void **)&lcd_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get LCD device config: %s", esp_err_to_name(ret));
            return;
        }

        // uint32_t fallback_lines = lcd_cfg->lcd_height < LVGL_FALLBACK_BUF_LINES ? lcd_cfg->lcd_height : LVGL_FALLBACK_BUF_LINES;
        // uint32_t lvgl_buffer_pixels = lcd_cfg->lcd_width * fallback_lines;
        uint32_t lvgl_buffer_pixels = lcd_cfg->lcd_width * lcd_cfg->lcd_height;
        bool lvgl_double_buffer = true;
        bool lvgl_use_spiram = true;

        lvgl_port_display_cfg_t disp_cfg = {
            .io_handle = io_handle,
            .panel_handle = panel_handle,
            .buffer_size = lvgl_buffer_pixels,
            .double_buffer = lvgl_double_buffer,
            .hres = lcd_cfg->lcd_width,
            .vres = lcd_cfg->lcd_height,
            .monochrome = false,
            .rotation = {
                .swap_xy = lcd_cfg->swap_xy,
                .mirror_x = lcd_cfg->mirror_x,
                .mirror_y = lcd_cfg->mirror_y,
            },
            .flags = {
                .buff_spiram = lvgl_use_spiram,
#if LVGL_VERSION_MAJOR >= 9
                .swap_bytes = false,
#endif
            }};

        ESP_LOGI(TAG, "LCD config - width: %d, height: %d, sub_type: %s",
                 lcd_cfg->lcd_width, lcd_cfg->lcd_height, lcd_cfg->sub_type);
        ESP_LOGI(TAG, "LVGL buffer - use_spiram: %d, pixels: %" PRIu32 ", double_buffer: %d",
                 lvgl_use_spiram, lvgl_buffer_pixels, lvgl_double_buffer);

        if (strcmp(lcd_cfg->sub_type, ESP_BOARD_DEVICE_LCD_SUB_TYPE_DSI) == 0) {
            lvgl_port_display_dsi_cfg_t dsi_cfg = {
                .flags = {
                    .avoid_tearing = false,
                },
            };
#if LVGL_VERSION_MAJOR >= 9
            disp_cfg.flags.swap_bytes = false;
#endif
            disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
            if (disp == NULL) {
                ESP_LOGE(TAG, "Failed to add DSI LCD display");
                return;
            }
        } else {
            disp = lvgl_port_add_disp(&disp_cfg);
            if (disp == NULL) {
                ESP_LOGE(TAG, "Failed to add LCD display");
                return;
            }
        }

        ESP_LOGI(TAG, "LCD display initialized successfully");

        ESP_LOGI(TAG, "Initializing backlight...");
            ret = esp_board_manager_get_device_handle("lcd_brightness", (void **)&ledc_handle);
        if (ret == ESP_OK && ledc_handle) {
            dev_ledc_ctrl_config_t *dev_ledc_cfg = NULL;
            ret = esp_board_manager_get_device_config(ESP_BOARD_DEVICE_NAME_LCD_BRIGHTNESS, (void *)&dev_ledc_cfg);
            periph_ledc_config_t *ledc_config = NULL;
            esp_board_manager_get_periph_config(dev_ledc_cfg->ledc_name, (void **)&ledc_config);
            uint32_t duty = (80 * ((1 << (uint32_t)ledc_config->duty_resolution) - 1)) / 100;
            ledc_set_duty(ledc_handle->speed_mode, ledc_handle->channel, duty);
            ledc_update_duty(ledc_handle->speed_mode, ledc_handle->channel);
            ESP_LOGI(TAG, "Backlight initialized and set to 80%% brightness");
        }

        ESP_LOGI(TAG, "Initializing touch panel...");
        ret = esp_board_manager_init_device_by_name("lcd_touch");
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize touch panel: %s", esp_err_to_name(ret));
        } else {
            ret = esp_board_manager_get_device_handle("lcd_touch", &touch_handle);
            if (ret == ESP_OK && touch_handle) {
                dev_lcd_touch_i2c_handles_t *touch_handles = (dev_lcd_touch_i2c_handles_t *)touch_handle;
                touch_panel_handle = touch_handles->touch_handle;

                lvgl_port_touch_cfg_t touch_cfg = {
                    .disp = disp,
                    .handle = touch_panel_handle,
                };

                lv_indev_t *touch_indev = lvgl_port_add_touch(&touch_cfg);
                if (touch_indev == NULL) {
                    ESP_LOGE(TAG, "Failed to add touch input device");
                } else {
                    ESP_LOGI(TAG, "Touch panel initialized successfully");
                }
            }
        }

        create_lvgl_ui();

        ESP_LOGI(TAG, "LVGL example running!");

        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    } else {
        ESP_LOGE(TAG, "LCD device handle is NULL");
    }
}