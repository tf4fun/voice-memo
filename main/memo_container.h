// main/memo_container.h —— 上传容器的头部布局（与服务端 docs/technical/server.md 一致）。
//
// 纯逻辑：只做字节摆放与回填，不碰文件系统，因此可以在宿主上测偏移。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MEMO_CONTAINER_HEADER_SIZE 80
#define MEMO_CONTAINER_VERSION 1
#define MEMO_CONTAINER_CODEC_OPUS 1

typedef struct {
    uint16_t sample_rate;
    uint8_t channels;
    uint8_t frame_ms;
    uint32_t duration_ms;
    uint64_t recorded_at;   /* UTC 秒；还没对时就是 0 */
    char memo_id[37];       /* 带连字符的 UUID，NUL 结尾 */
    uint32_t frame_count;
} memo_container_header_t;

/* 用 16 字节随机数生成 UUIDv4 文本（36 字符 + NUL）。 */
void memo_container_make_uuid(char out[37], const uint8_t random[16]);

/* 编码成 80 字节头部。frame_count / duration 由调用方在收尾时回填。 */
void memo_container_encode(const memo_container_header_t *header,
                           uint8_t out[MEMO_CONTAINER_HEADER_SIZE]);

/* 就地回填帧数与时长（停止录音后调用）。 */
void memo_container_patch(uint8_t buffer[MEMO_CONTAINER_HEADER_SIZE],
                          uint32_t frame_count,
                          uint32_t duration_ms);

/* 解出头部，主要供测试与排查使用。 */
bool memo_container_decode(const uint8_t buffer[MEMO_CONTAINER_HEADER_SIZE],
                           memo_container_header_t *out);
