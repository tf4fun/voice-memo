# The voice memo application shell replaced the baseline hardware-test menu, so
# only the application sources are compiled. The demo pages stay in the tree as
# upstream reference material and are still exercised by the host tests that
# tools/validate.sh compiles directly.
idf_component_register(
    SRCS "memo_app.c"
    INCLUDE_DIRS "."
    REQUIRES bsp
)
