/* 宿主测试：上传容器头部的字节布局。
 *
 * 这些偏移必须与服务端 internal/domain/memo.go 完全一致，否则服务端会把帧数读成
 * 别的字段。测试在这里把偏移写死，正是为了让两边一旦漂移就立刻失败。
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "memo_container.h"

static void test_header_size(void)
{
    assert(MEMO_CONTAINER_HEADER_SIZE == 80);
}

static void test_field_offsets(void)
{
    memo_container_header_t header = {
        .sample_rate = 16000,
        .channels = 1,
        .frame_ms = 60,
        .duration_ms = 1380,
        .recorded_at = 1761468120ULL,
        .frame_count = 23,
    };
    strcpy(header.memo_id, "9f1e5c7a-1111-4222-8333-444455556666");

    uint8_t raw[MEMO_CONTAINER_HEADER_SIZE];
    memo_container_encode(&header, raw);

    /* magic / version / codec */
    assert(memcmp(raw + 0, "MEMO", 4) == 0);
    assert(raw[4] == 1);
    assert(raw[5] == 1);

    /* 采样率小端 */
    assert(raw[6] == 0x80 && raw[7] == 0x3E);
    assert(raw[8] == 1);
    assert(raw[9] == 60);

    /* 时长小端 1380 = 0x00000564 */
    assert(raw[12] == 0x64 && raw[13] == 0x05 && raw[14] == 0 && raw[15] == 0);

    /* recorded_at 小端 8 字节 */
    uint64_t stamp = 0;
    for (int i = 0; i < 8; i++) stamp |= (uint64_t)raw[16 + i] << (8 * i);
    assert(stamp == header.recorded_at);

    /* memo id 从 24 起，占 36 字节 */
    assert(memcmp(raw + 24, header.memo_id, 36) == 0);

    /* 帧数在 60 起，4 字节小端 */
    uint32_t frames = (uint32_t)raw[60] | ((uint32_t)raw[61] << 8) |
                      ((uint32_t)raw[62] << 16) | ((uint32_t)raw[63] << 24);
    assert(frames == header.frame_count);

    /* 保留区必须为 0，避免设备端把垃圾写进协议字段 */
    for (int i = 64; i < 80; i++) assert(raw[i] == 0);
    for (int i = 10; i < 12; i++) assert(raw[i] == 0);
}

static void test_decode_round_trip(void)
{
    memo_container_header_t in = {
        .sample_rate = 16000,
        .channels = 1,
        .frame_ms = 60,
        .duration_ms = 60000,
        .recorded_at = 1700000000ULL,
        .frame_count = 1000,
    };
    strcpy(in.memo_id, "abcdef01-2345-4678-9abc-def012345678");

    uint8_t raw[MEMO_CONTAINER_HEADER_SIZE];
    memo_container_encode(&in, raw);

    memo_container_header_t out = {0};
    assert(memo_container_decode(raw, &out));
    assert(out.sample_rate == in.sample_rate);
    assert(out.channels == in.channels);
    assert(out.frame_ms == in.frame_ms);
    assert(out.duration_ms == in.duration_ms);
    assert(out.recorded_at == in.recorded_at);
    assert(out.frame_count == in.frame_count);
    assert(strcmp(out.memo_id, in.memo_id) == 0);
}

static void test_patch_updates_counters_only(void)
{
    memo_container_header_t header = {
        .sample_rate = 16000,
        .channels = 1,
        .frame_ms = 60,
        .duration_ms = 0,
        .recorded_at = 1761468120ULL,
        .frame_count = 0,
    };
    strcpy(header.memo_id, "11111111-2222-4333-8444-555566667777");

    uint8_t raw[MEMO_CONTAINER_HEADER_SIZE];
    memo_container_encode(&header, raw);

    memo_container_patch(raw, 300, 18000);

    memo_container_header_t out = {0};
    assert(memo_container_decode(raw, &out));
    assert(out.frame_count == 300);
    assert(out.duration_ms == 18000);
    /* 其余字段不受影响 */
    assert(out.recorded_at == header.recorded_at);
    assert(strcmp(out.memo_id, header.memo_id) == 0);
}

static void test_decode_rejects_foreign_buffer(void)
{
    uint8_t raw[MEMO_CONTAINER_HEADER_SIZE];
    memset(raw, 0, sizeof(raw));
    memcpy(raw, "NOPE", 4);

    memo_container_header_t out;
    assert(!memo_container_decode(raw, &out));
}

static void test_uuid_format(void)
{
    uint8_t random[16];
    for (int i = 0; i < 16; i++) random[i] = (uint8_t)(0xA0 + i);

    char id[37];
    memo_container_make_uuid(id, random);

    assert(strlen(id) == 36);
    /* 连字符位置 */
    assert(id[8] == '-' && id[13] == '-' && id[18] == '-' && id[23] == '-');
    /* UUIDv4: 第三段以 4 开头；第四段首字符在 8/9/a/b 之一 */
    assert(id[14] == '4');
    assert(id[19] == '8' || id[19] == '9' || id[19] == 'a' || id[19] == 'b');

    /* 全 0 与全 F 也要产出合法格式 */
    uint8_t zeros[16] = {0};
    memo_container_make_uuid(id, zeros);
    assert(strlen(id) == 36 && id[14] == '4');

    uint8_t ones[16];
    memset(ones, 0xFF, sizeof(ones));
    memo_container_make_uuid(id, ones);
    assert(strlen(id) == 36 && id[14] == '4');
}

static void test_uuid_is_deterministic(void)
{
    uint8_t random[16];
    for (int i = 0; i < 16; i++) random[i] = (uint8_t)(i * 7);

    char first[37], second[37];
    memo_container_make_uuid(first, random);
    memo_container_make_uuid(second, random);
    assert(strcmp(first, second) == 0);
}

int main(void)
{
    test_header_size();
    test_field_offsets();
    test_decode_round_trip();
    test_patch_updates_counters_only();
    test_decode_rejects_foreign_buffer();
    test_uuid_format();
    test_uuid_is_deterministic();
    puts("memo container: PASS");
    return 0;
}
