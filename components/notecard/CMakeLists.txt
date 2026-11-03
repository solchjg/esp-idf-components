file(GLOB NOTE_C_SRCS "${CMAKE_CURRENT_LIST_DIR}/note-c/*.c")
idf_component_register(SRCS "notecard.c" "notecard_core.c" ${NOTE_C_SRCS}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES esp_common
                       PRIV_REQUIRES esp_driver_i2c esp_timer freertos esp_try)
# note-c is vendored unchanged: keep its warnings from failing the build.
set_source_files_properties(${NOTE_C_SRCS} PROPERTIES COMPILE_OPTIONS "-Wno-error")
