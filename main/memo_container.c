// main/memo_container.c —— 头部字节摆放。偏移必须与服务端解析一致。
#include "memo_container.h"

#include <stdio.h>
#include <string.h>

/* 偏移表（改这里就要同时改服务端 internal/domain/memo.go）:
   0   4  magic "MEMO"
   4   1  version
   5   1  codec
   6   2  sample rate
   8   1  channels
   9   1  frame ms
   10  2  reserved
   12  4  duration ms
   16  8  recorded at (unix seconds)
   24  36 memo id (dashed uuid)
   60  4  frame count
   64  16 reserved
*/
enum {
    OFF_MAGIC = 0,
    OFF_VERSION = 4,
    OFF_CODEC = 5,
    OFF_RATE = 6,
    OFF_CHANNELS = 8,
    OFF_FRAME_MS = 9,
    OFF_DURATION = 12,
    OFF_RECORDED_AT = 16,
    OFF_MEMO_ID = 24,
    OFF_FRAME_COUNT = 60,
};

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void put_u64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) {
        p[i] = (uint8_t)((v >> (8 * i)) & 0xFF);
    }
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t get_u64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v |= (uint64_t)p[i] << (8 * i);
    }
    return v;
}

void memo_container_make_uuid(char out[37], const uint8_t random[16])
{
    /* UUIDv4: 版本位 0100，变体位 10。 */
    uint8_t bytes[16];
    memcpy(bytes, random, sizeof(bytes));
    bytes[6] = (uint8_t)((bytes[6] & 0x0F) | 0x40);
    bytes[8] = (uint8_t)((bytes[8] & 0x3F) | 0x80);

    snprintf(out, 37,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             bytes[0], bytes[1], bytes[2], bytes[3],
             bytes[4], bytes[5], bytes[6], bytes[7],
             bytes[8], bytes[9], bytes[10], bytes[11],
             bytes[12], bytes[13], bytes[14], bytes[15]);
}

void memo_container_encode(const memo_container_header_t *header,
                           uint8_t out[MEMO_CONTAINER_HEADER_SIZE])
{
    memset(out, 0, MEMO_CONTAINER_HEADER_SIZE);

    memcpy(out + OFF_MAGIC, "MEMO", 4);
    out[OFF_VERSION] = MEMO_CONTAINER_VERSION;
    out[OFF_CODEC] = MEMO_CONTAINER_CODEC_OPUS;
    put_u16(out + OFF_RATE, header->sample_rate);
    out[OFF_CHANNELS] = header->channels;
    out[OFF_FRAME_MS] = header->frame_ms;
    put_u32(out + OFF_DURATION, header->duration_ms);
    put_u64(out + OFF_RECORDED_AT, header->recorded_at);
    strncpy((char *)(out + OFF_MEMO_ID), header->memo_id, 36);
    put_u32(out + OFF_FRAME_COUNT, header->frame_count);
}

void memo_container_patch(uint8_t buffer[MEMO_CONTAINER_HEADER_SIZE],
                          uint32_t frame_count,
                          uint32_t duration_ms)
{
    put_u32(buffer + OFF_DURATION, duration_ms);
    put_u32(buffer + OFF_FRAME_COUNT, frame_count);
}

bool memo_container_decode(const uint8_t buffer[MEMO_CONTAINER_HEADER_SIZE],
                           memo_container_header_t *out)
{
    if (memcmp(buffer + OFF_MAGIC, "MEMO", 4) != 0) return false;
    if (buffer[OFF_VERSION] != MEMO_CONTAINER_VERSION) return false;
    if (buffer[OFF_CODEC] != MEMO_CONTAINER_CODEC_OPUS) return false;

    out->sample_rate = get_u16(buffer + OFF_RATE);
    out->channels = buffer[OFF_CHANNELS];
    out->frame_ms = buffer[OFF_FRAME_MS];
    out->duration_ms = get_u32(buffer + OFF_DURATION);
    out->recorded_at = get_u64(buffer + OFF_RECORDED_AT);
    memcpy(out->memo_id, buffer + OFF_MEMO_ID, 36);
    out->memo_id[36] = '\0';
    out->frame_count = get_u32(buffer + OFF_FRAME_COUNT);
    return true;
}
