#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_board_manager_includes.h"
#include "driver/ledc.h"
#include "periph_ledc.h"
#include "esp_lcd_panel_ops.h"
#include "logo_labplus_ledong_v2_320x172_lcd.h"
#include "esp_audio_simple_player.h"
#include "dev_audio_codec.h"
#include "esp_codec_dev.h"

static const char *TAG = "MAIN";

static esp_codec_dev_handle_t g_codec_dev = NULL;

static int out_data_callback(uint8_t *data, int data_size, void *ctx)
{
    if (g_codec_dev) {
        return esp_codec_dev_write(g_codec_dev, data, data_size);
    }
    return 0;
}

static int mock_event_callback(esp_asp_event_pkt_t *event, void *ctx)
{
    if (event->type == ESP_ASP_EVENT_TYPE_MUSIC_INFO) {
        esp_asp_music_info_t info = {0};
        memcpy(&info, event->payload, event->payload_size);
        ESP_LOGI(TAG, "Music info: sample_rate=%d, channels=%d, bits=%d",
                 info.sample_rate, info.channels, info.bits);
        esp_codec_dev_sample_info_t fs = {
            .sample_rate = info.sample_rate,
            .channel = info.channels,
            .bits_per_sample = info.bits,
        };
        int ret = esp_codec_dev_open(g_codec_dev, &fs);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Failed to open codec device: %d", ret);
        }
        ret = esp_codec_dev_set_out_vol(g_codec_dev, 80);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "Failed to set volume: %d", ret);
        } else {
            ESP_LOGI(TAG, "Volume set to 80%%");
        }
    } else if (event->type == ESP_ASP_EVENT_TYPE_STATE) {
        esp_asp_state_t st = 0;
        memcpy(&st, event->payload, event->payload_size);
        ESP_LOGI(TAG, "State: %d, %s", st, esp_audio_simple_player_state_to_str(st));
        if (st == ESP_ASP_STATE_STOPPED || st == ESP_ASP_STATE_FINISHED) {
            esp_codec_dev_close(g_codec_dev);
        }
    }
    return 0;
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI(TAG, "Initializing board manager...");
    int ret = esp_board_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize board manager");
        return;
    }

    dev_display_lcd_handles_t *lcd_handle;
    ret = esp_board_manager_get_device_handle("display_lcd", (void **)&lcd_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LCD device");
        return;
    }

    periph_ledc_handle_t *ledc_handle;
    ret = esp_board_manager_get_device_handle("lcd_brightness", (void **)&ledc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LEDC device");
        return;
    }
    ledc_set_duty(ledc_handle->speed_mode, ledc_handle->channel, 50);
    ledc_update_duty(ledc_handle->speed_mode, ledc_handle->channel);
    uint16_t *color_data=heap_caps_malloc(600*1024 * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    memcpy(color_data, logo_en_320x172_lcd, 1024 * 600 * sizeof(uint16_t));
    esp_lcd_panel_draw_bitmap(lcd_handle->panel_handle, 0, 0,1024,600, logo_en_320x172_lcd);

    ESP_LOGI(TAG, "Initializing SD card...");
    ret = esp_board_manager_init_device_by_name("fs_sdcard");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init SD card");
        return;
    }

    ESP_LOGI(TAG, "Initializing audio DAC...");
    ret = esp_board_manager_init_device_by_name("audio_dac");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init audio DAC");
        return;
    }

    dev_audio_codec_handles_t *audio_dac_handle;
    ret = esp_board_manager_get_device_handle("audio_dac", (void **)&audio_dac_handle);
    if (ret != ESP_OK || audio_dac_handle == NULL) {
        ESP_LOGE(TAG, "Failed to get audio DAC handle");
        return;
    }
    g_codec_dev = audio_dac_handle->codec_dev;

    esp_asp_cfg_t asp_cfg = {
        .in = {
            .cb = NULL,
            .user_ctx = NULL,
        },
        .out = {
            .cb = out_data_callback,
            .user_ctx = NULL,
        },
        .task_prio = 5,
        .task_stack = 8 * 1024,
    };

    esp_asp_handle_t player = NULL;
    esp_gmf_err_t gmf_err = esp_audio_simple_player_new(&asp_cfg, &player);
    if (gmf_err != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to create audio player: %d", gmf_err);
        return;
    }

    esp_audio_simple_player_set_event(player, mock_event_callback, NULL);

    ESP_LOGI(TAG, "Playing audio from file://sdcard/test.mp3...");
    gmf_err = esp_audio_simple_player_run_to_end(player, "file://sdcard/test.mp3", NULL);
    if (gmf_err != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to play audio: %d", gmf_err);
        esp_audio_simple_player_destroy(player);
        return;
    }

    ESP_LOGI(TAG, "Playback completed!");

    esp_audio_simple_player_destroy(player);

    esp_board_manager_print_board_info();
    esp_board_manager_print();
}
