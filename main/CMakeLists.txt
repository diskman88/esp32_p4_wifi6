idf_component_register(SRCS "mpython_idf.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_audio_simple_player esp_board_manager esp_codec_dev lvgl)