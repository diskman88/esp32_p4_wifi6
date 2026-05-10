#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_board_manager_includes.h"
#include "driver/ledc.h"
#include "periph_ledc.h"
#include "esp_lcd_panel_ops.h"
#include "dev_display_lcd.h"
#include "dev_camera.h"
#include "esp_video_device.h"
#include "linux/videodev2.h"

static const char *TAG = "CAMERA_DISPLAY";

void display_test_pattern(dev_display_lcd_handles_t *lcd_handle);

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI(TAG, "Initializing board manager...");
    
    int ret = esp_board_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize board manager");
        return;
    }

    ESP_LOGI(TAG, "Getting LCD device handle...");
    dev_display_lcd_handles_t *lcd_handle;
    ret = esp_board_manager_get_device_handle("display_lcd", (void **)&lcd_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LCD device");
        return;
    }

    ESP_LOGI(TAG, "Setting LCD brightness...");
    periph_ledc_handle_t *ledc_handle;
    ret = esp_board_manager_get_device_handle("lcd_brightness", (void **)&ledc_handle);
    if (ret == ESP_OK) {
        ledc_set_duty(ledc_handle->speed_mode, ledc_handle->channel, 0);
        ledc_update_duty(ledc_handle->speed_mode, ledc_handle->channel);
    }

    ESP_LOGI(TAG, "Initializing camera...");
    ret = esp_board_manager_init_device_by_name("camera");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize camera");
        ESP_LOGI(TAG, "Displaying test pattern instead...");
        display_test_pattern(lcd_handle);
        return;
    }

    ESP_LOGI(TAG, "Getting camera device handle...");
    dev_camera_handle_t *camera_handle;
    ret = esp_board_manager_get_device_handle("camera", (void **)&camera_handle);
    if (ret != ESP_OK || camera_handle == NULL) {
        ESP_LOGE(TAG, "Failed to get camera handle");
        ESP_LOGI(TAG, "Displaying test pattern instead...");
        display_test_pattern(lcd_handle);
        return;
    }
    
    ESP_LOGI(TAG, "Camera device path: %s", camera_handle->dev_path);

    ESP_LOGI(TAG, "Opening camera device...");
    int fd = open(camera_handle->dev_path, O_RDWR);
    if (fd < 0) {
        ESP_LOGE(TAG, "Failed to open camera device: %s", camera_handle->dev_path);
        ESP_LOGI(TAG, "Displaying test pattern instead...");
        display_test_pattern(lcd_handle);
        return;
    }

    ESP_LOGI(TAG, "Configuring camera format...");
    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 800;
    fmt.fmt.pix.height = 640;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    ret = ioctl(fd, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to set format");
        close(fd);
        ESP_LOGI(TAG, "Displaying test pattern instead...");
        display_test_pattern(lcd_handle);
        return;
    }

    ESP_LOGI(TAG, "Camera configured: %dx%d, format=%d", 
             fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.pixelformat);

    ESP_LOGI(TAG, "Requesting buffer...");
    struct v4l2_requestbuffers req = {0};
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    ret = ioctl(fd, VIDIOC_REQBUFS, &req);
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to request buffers");
        close(fd);
        ESP_LOGI(TAG, "Displaying test pattern instead...");
        display_test_pattern(lcd_handle);
        return;
    }

    ESP_LOGI(TAG, "Requested %d buffers", req.count);

    void **buffers = calloc(req.count, sizeof(void *));
    if (!buffers) {
        ESP_LOGE(TAG, "Failed to allocate buffer pointers");
        close(fd);
        ESP_LOGI(TAG, "Displaying test pattern instead...");
        display_test_pattern(lcd_handle);
        return;
    }

    ESP_LOGI(TAG, "Mapping buffers...");
    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    for (uint32_t i = 0; i < req.count; i++) {
        buf.index = i;
        ret = ioctl(fd, VIDIOC_QUERYBUF, &buf);
        if (ret < 0) {
            ESP_LOGE(TAG, "Failed to query buffer %d", i);
            for (uint32_t j = 0; j < i; j++) {
                munmap(buffers[j], buf.length);
            }
            free(buffers);
            close(fd);
            ESP_LOGI(TAG, "Displaying test pattern instead...");
            display_test_pattern(lcd_handle);
            return;
        }

        buffers[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (buffers[i] == MAP_FAILED) {
            ESP_LOGE(TAG, "Failed to mmap buffer %d", i);
            for (uint32_t j = 0; j < i; j++) {
                munmap(buffers[j], buf.length);
            }
            free(buffers);
            close(fd);
            ESP_LOGI(TAG, "Displaying test pattern instead...");
            display_test_pattern(lcd_handle);
            return;
        }

        ret = ioctl(fd, VIDIOC_QBUF, &buf);
        if (ret < 0) {
            ESP_LOGE(TAG, "Failed to queue buffer %d", i);
        }
    }

    ESP_LOGI(TAG, "Starting capture...");
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(fd, VIDIOC_STREAMON, &type);
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to start streaming");
        for (uint32_t i = 0; i < req.count; i++) {
            munmap(buffers[i], buf.length);
        }
        free(buffers);
        close(fd);
        ESP_LOGI(TAG, "Displaying test pattern instead...");
        display_test_pattern(lcd_handle);
        return;
    }

    ESP_LOGI(TAG, "Capturing and displaying...");
    while (1) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        ret = ioctl(fd, VIDIOC_DQBUF, &buf);
        if (ret < 0) {
            ESP_LOGE(TAG, "Failed to dequeue buffer");
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        esp_lcd_panel_draw_bitmap(lcd_handle->panel_handle, 0, 0,
                                  fmt.fmt.pix.width, fmt.fmt.pix.height, buffers[buf.index]);

        ret = ioctl(fd, VIDIOC_QBUF, &buf);
        if (ret < 0) {
            ESP_LOGE(TAG, "Failed to re-queue buffer");
        }
    }

    ioctl(fd, VIDIOC_STREAMOFF, &type);
    for (uint32_t i = 0; i < req.count; i++) {
        munmap(buffers[i], buf.length);
    }
    free(buffers);
    close(fd);
    esp_board_manager_print_board_info();
}

void display_test_pattern(dev_display_lcd_handles_t *lcd_handle)
{
    ESP_LOGI(TAG, "Displaying color bars test pattern...");
    
    uint16_t *test_buffer = heap_caps_malloc(1024 * 600 * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!test_buffer) {
        ESP_LOGE(TAG, "Failed to allocate test buffer");
        return;
    }

    for (int y = 0; y < 600; y++) {
        for (int x = 0; x < 1024; x++) {
            uint8_t bar = (x * 8) / 1024;
            uint16_t color;
            switch (bar) {
                case 0: color = 0xF800; break; // Red
                case 1: color = 0x07E0; break; // Green
                case 2: color = 0x001F; break; // Blue
                case 3: color = 0xFFE0; break; // Yellow
                case 4: color = 0xF81F; break; // Magenta
                case 5: color = 0x07FF; break; // Cyan
                case 6: color = 0xFFFF; break; // White
                default: color = 0x0000; break; // Black
            }
            test_buffer[y * 1024 + x] = color;
        }
    }

    esp_lcd_panel_draw_bitmap(lcd_handle->panel_handle, 0, 0, 1024, 600, test_buffer);
    
    free(test_buffer);
    
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
