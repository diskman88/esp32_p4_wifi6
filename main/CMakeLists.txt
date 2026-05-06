idf_component_register(SRCS "wav_header.c" "mpython_idf.c"
                    INCLUDE_DIRS "." "include"
                    REQUIRES esp_audio_simple_player esp_board_manager esp_codec_dev)
