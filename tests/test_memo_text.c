/* 宿主测试：UTF-8 截断与省略号。重点是绝不切坏多字节字符。 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "memo_text.h"

static void expect_str(const char *got, const char *want, const char *what)
{
    if (strcmp(got, want) != 0) {
        printf("FAIL %s: got \"%s\", want \"%s\"\n", what, got, want);
        assert(0);
    }
}

static void test_char_count(void)
{
    assert(memo_text_chars("") == 0);
    assert(memo_text_chars("abc") == 3);
    assert(memo_text_chars("下午三点") == 4);
    /* 中英混排 */
    assert(memo_text_chars("下午3点ok") == 6);
    /* emoji 是 4 字节，算一个字符 */
    assert(memo_text_chars("好\xF0\x9F\x98\x80") == 2);
    /* 结束处被截断的序列也算一个字符，不越界 */
    assert(memo_text_chars("\xE4\xB8") == 1);
}

static void test_byte_offset(void)
{
    const char *s = "下午三点";
    assert(memo_text_byte_offset(s, 0) == 0);
    assert(memo_text_byte_offset(s, 1) == 3);
    assert(memo_text_byte_offset(s, 4) == 12);
    assert(memo_text_byte_offset(s, 99) == 12);
}

static void test_prefix(void)
{
    char buf[16];

    memo_text_prefix(buf, sizeof(buf), "下午三点和张工", 4);
    expect_str(buf, "下午三点", "prefix 4");

    memo_text_prefix(buf, sizeof(buf), "abcdef", 3);
    expect_str(buf, "abc", "ascii prefix");

    memo_text_prefix(buf, sizeof(buf), "abc", 10);
    expect_str(buf, "abc", "prefix longer than text");

    /* 缓冲区只够两个汉字：必须整字截断，不能留下半个。 */
    char small[7];
    memo_text_prefix(small, sizeof(small), "下午三点", 10);
    assert(memo_text_is_valid_utf8(small));
    expect_str(small, "下午", "prefix limited by capacity");
}

static void test_ellipsis(void)
{
    char buf[32];

    memo_text_ellipsis(buf, sizeof(buf), "下午三点和张工对了下进度", 6);
    assert(memo_text_chars(buf) == 6);
    expect_str(buf, "下午三点和…", "ellipsis keeps 5 chars + ellipsis");

    /* 本来就够短：不加省略号 */
    memo_text_ellipsis(buf, sizeof(buf), "短", 6);
    expect_str(buf, "短", "no ellipsis when it fits");

    /* 正好等于上限：不加省略号 */
    memo_text_ellipsis(buf, sizeof(buf), "一二三四五六", 6);
    expect_str(buf, "一二三四五六", "exact fit");

    /* 上限为 1：只能是省略号本身 */
    memo_text_ellipsis(buf, sizeof(buf), "一二三", 1);
    expect_str(buf, "…", "single char budget");

    /* 空串 */
    memo_text_ellipsis(buf, sizeof(buf), "", 5);
    expect_str(buf, "", "empty input");
}

static void test_utf8_validity(void)
{
    assert(memo_text_is_valid_utf8("下午三点"));
    assert(memo_text_is_valid_utf8(""));
    assert(!memo_text_is_valid_utf8("\xE4\xB8"));
    assert(!memo_text_is_valid_utf8("a\x80" "b"));
}

int main(void)
{
    test_char_count();
    test_byte_offset();
    test_prefix();
    test_ellipsis();
    test_utf8_validity();
    puts("memo text: PASS");
    return 0;
}
