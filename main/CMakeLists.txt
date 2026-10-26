# The voice memo application shell replaced the baseline hardware-test menu, so
# only the application sources are compiled. The demo pages stay in the tree as
# upstream reference material and are still exercised by the host tests that
# tools/validate.sh compiles directly.
idf_component_register(
    SRCS "memo_app.c"
         "memo_state.c"
         "memo_text.c"
         "memo_time.c"
         "memo_container.c"
         "memo_spool.c"
         "memo_recorder.c"
         "memo_player.c"
         "memo_font.c"
         "memo_ui_common.c"
         "memo_ui_detail.c"
         "memo_ui_settings.c"
         "memo_settings.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_audio_codec esp_timer nvs_flash spiffs
)

# Generated CJK subsets live outside main/ so they stay next to the fonts they
# were produced from; see assets/fonts/README.md for the exact command.
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/memo_font_16.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/memo_font_20.c"
)
