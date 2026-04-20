cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(espcam)

target_sources(app PRIVATE
    src/main.c
    src/wifi_manager.c
    src/frame_source.c
    src/http_server.c
    src/http_api.c
    src/stream_handler.c
    src/camera_init.c
    src/camera_config.c
    src/camera_sccb.c
    src/cam_i2s_capture.c
    src/cam_i2s_hw.c
    src/cam_jpeg_fixup.c
    src/led_control.c
    src/ws_protocol.c
)
