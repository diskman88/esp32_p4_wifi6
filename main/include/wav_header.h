#pragma once

#include <stdint.h>
#include <stdio.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t read_wav_header(FILE *fp, uint32_t *sample_rate, uint16_t *channels, uint16_t *bits_per_sample);

esp_err_t write_wav_header(FILE *fp, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample, uint32_t duration_seconds);

#ifdef __cplusplus
}
#endif
