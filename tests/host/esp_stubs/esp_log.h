#pragma once

// Host stand-in for ESP-IDF's logging macros. See esp_heap_caps.h alongside
// for why these are shims rather than #ifdefs in the modules themselves.
//
// Everything goes to stderr, which keeps it out of the test runner's own
// PASS/FAIL stream on stdout while still being there to read when a case
// fails - the decode log line ("cover 384x384 -> ...") is exactly what tells
// you which stage of the pipeline went wrong.

#include <stdio.h>

#define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "E %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) fprintf(stderr, "W %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) fprintf(stderr, "I %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) fprintf(stderr, "D %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...) fprintf(stderr, "V %s: " fmt "\n", tag, ##__VA_ARGS__)
