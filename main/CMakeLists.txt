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
    INCLUDE_DIRS "."
    REQUIRES bsp esp_audio_codec esp_timer nvs_flash spiffs
)

# The CJK font subsets land here in M3.
