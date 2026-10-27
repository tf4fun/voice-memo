#pragma once
static inline void test_log(const char *tag, const char *format, ...)
{ (void)tag; (void)format; }
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_LOGE(...) test_log(__VA_ARGS__)
