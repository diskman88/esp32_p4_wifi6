#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_board_manager_includes.h"
#include "wav_header.h"

static const char *TAG = "RECORD_AND_PLAY";

#define DEFAULT_REC_URL          "/sdcard/test.wav"
#define DEFAULT_SAMPLE_RATE      16000
#define DEFAULT_CHANNELS         2
#define DEFAULT_BITS_PER_SAMPLE  16
#define DEFAULT_DURATION_SECONDS 5
#define DEFAULT_REC_GAIN         40
#define DEFAULT_PLAY_VOL         100

static esp_err_t record_audio_to_sdcard(void)
{
    ESP_LOGI(TAG, "Recording to %s", DEFAULT_REC_URL);
    esp_err_t ret = ESP_OK;
    dev_audio_codec_handles_t *adc_handle = NULL;
    FILE *fp = NULL;

    const size_t buffer_size = 4096;
    uint8_t *recording_buffer = malloc(buffer_size);
    if (recording_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate recording buffer");
        return ESP_FAIL;
    }

    ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_ADC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize audio ADC device");
        free(recording_buffer);
        return ret;
    }

    ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card");
        esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_ADC);
        free(recording_buffer);
        return ret;
    }

    ret = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_ADC, (void **)&adc_handle);
    if (ret != ESP_OK || adc_handle == NULL) {
        ESP_LOGE(TAG, "Failed to get ADC device handle");
        esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
        esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_ADC);
        free(recording_buffer);
        return ret;
    }

    fp = fopen(DEFAULT_REC_URL, "wb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "Failed to open file %s", DEFAULT_REC_URL);
        esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
        esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_ADC);
        free(recording_buffer);
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = DEFAULT_SAMPLE_RATE,
        .channel = DEFAULT_CHANNELS,
        .bits_per_sample = DEFAULT_BITS_PER_SAMPLE,
    };
    ret = esp_codec_dev_open(adc_handle->codec_dev, &fs);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open codec device");
        fclose(fp);
        esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
        esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_ADC);
        free(recording_buffer);
        return ESP_FAIL;
    }

    ret = esp_codec_dev_set_in_gain(adc_handle->codec_dev, DEFAULT_REC_GAIN);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Failed to set ADC volume, continuing anyway");
    }

    ret = write_wav_header(fp, fs.sample_rate, fs.channel, fs.bits_per_sample, DEFAULT_DURATION_SECONDS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write WAV header");
        esp_codec_dev_close(adc_handle->codec_dev);
        fclose(fp);
        esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
        esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_ADC);
        free(recording_buffer);
        return ret;
    }

    ESP_LOGI(TAG, "Record WAV file info: %" PRIu32 " Hz, %" PRIu16 " channels, %" PRIu16 " bits",
             fs.sample_rate, fs.channel, fs.bits_per_sample);

    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "Starting recording...");
    uint32_t total_bytes = 0;
    uint32_t record_duration_ms = DEFAULT_DURATION_SECONDS * 1000;
    uint32_t start_time = esp_timer_get_time() / 1000;

    while ((esp_timer_get_time() / 1000) - start_time < record_duration_ms) {
        ret = esp_codec_dev_read(adc_handle->codec_dev, recording_buffer, buffer_size);
        if (ret == ESP_CODEC_DEV_OK) {
            size_t bytes_written = fwrite(recording_buffer, 1, buffer_size, fp);
            if (bytes_written != buffer_size) {
                ESP_LOGE(TAG, "Failed to write audio data to file");
                break;
            }
            total_bytes += bytes_written;
        } else {
            ESP_LOGE(TAG, "Failed to read audio data from ADC");
            break;
        }
        ESP_LOGI(TAG, "Recording... %" PRIu32 " ms", (esp_timer_get_time() / 1000) - start_time);
    }

    ESP_LOGI(TAG, "Recording completed. Total bytes recorded: %" PRIu32, total_bytes);
    esp_codec_dev_close(adc_handle->codec_dev);
    free(recording_buffer);
    fclose(fp);

    ret = esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_ADC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize audio ADC device");
        return ret;
    }

    return ESP_OK;
}

static esp_err_t play_audio_from_sdcard(void)
{
    ESP_LOGI(TAG, "Playing from %s", DEFAULT_REC_URL);
    esp_err_t ret = ESP_OK;
    dev_audio_codec_handles_t *dac_handle = NULL;
    FILE *fp = NULL;
    uint8_t *playback_buffer = NULL;

    ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize audio DAC device");
        return ret;
    }

    ret = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC, (void **)&dac_handle);
    if (ret != ESP_OK || dac_handle == NULL) {
        ESP_LOGE(TAG, "Failed to get DAC device handle");
        esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
        return ret;
    }

    fp = fopen(DEFAULT_REC_URL, "rb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "Failed to open file %s", DEFAULT_REC_URL);
        esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
        return ESP_FAIL;
    }

    uint32_t sample_rate;
    uint16_t channels, bits_per_sample;
    ret = read_wav_header(fp, &sample_rate, &channels, &bits_per_sample);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read WAV header");
        fclose(fp);
        esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
        return ret;
    }

    ESP_LOGI(TAG, "Play WAV file info: %" PRIu32 " Hz, %" PRIu16 " channels, %" PRIu16 " bits",
             sample_rate, channels, bits_per_sample);

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = sample_rate,
        .channel = channels,
        .bits_per_sample = bits_per_sample,
    };
    ret = esp_codec_dev_open(dac_handle->codec_dev, &fs);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open codec device");
        fclose(fp);
        esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
        return ESP_FAIL;
    }

    ret = esp_codec_dev_set_out_vol(dac_handle->codec_dev, DEFAULT_PLAY_VOL);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Failed to set DAC volume, continuing anyway");
    }

    const size_t buffer_size = 1 * 1024;
    playback_buffer = malloc(buffer_size);
    if (playback_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate playback buffer");
        esp_codec_dev_close(dac_handle->codec_dev);
        fclose(fp);
        esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Starting playback...");
    size_t bytes_read;
    while ((bytes_read = fread(playback_buffer, 1, buffer_size, fp)) > 0) {
        ret = esp_codec_dev_write(dac_handle->codec_dev, playback_buffer, bytes_read);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Failed to write to DAC");
            break;
        }
    }

    ESP_LOGI(TAG, "Playback completed.");
    free(playback_buffer);
    fclose(fp);
    esp_codec_dev_close(dac_handle->codec_dev);

    ret = esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize audio DAC device");
        return ret;
    }

    ret = esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize SD card filesystem");
        return ret;
    }

    return ESP_OK;
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

    ESP_LOGI(TAG, "=== Recording Audio ===");
    ret = record_audio_to_sdcard();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Recording failed");
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "=== Playing Audio ===");
    ret = play_audio_from_sdcard();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Playback failed");
        return;
    }

    ESP_LOGI(TAG, "Record and Play example finished!");
    esp_board_manager_print_board_info();
}
