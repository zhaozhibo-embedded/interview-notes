#include "ring_buffer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_init(void)
{
    rb_t rb;
    uint8_t mem[8];

    assert(rb_init(&rb, mem, 8) == 0);
    assert(rb_init(&rb, mem, 7) == -1);   /* 非 2 的幂 */
    assert(rb_init(&rb, mem, 0) == -1);
    assert(rb_init(NULL, mem, 8) == -1);
    assert(rb_init(&rb, NULL, 8) == -1);

    rb_init(&rb, mem, 8);
    assert(rb_used(&rb) == 0);
    assert(rb_free(&rb) == 8);
}

static void test_single(void)
{
    rb_t rb;
    uint8_t mem[4];
    uint8_t b = 0;

    rb_init(&rb, mem, 4);
    assert(rb_get(&rb, &b) == -1);        /* 空读 */

    assert(rb_put(&rb, 0x5A) == 0);
    assert(rb_used(&rb) == 1);
    assert(rb_free(&rb) == 3);
    assert(rb_get(&rb, &b) == 0);
    assert(b == 0x5A);
    assert(rb_used(&rb) == 0);
}

static void test_full(void)
{
    rb_t rb;
    uint8_t mem[4];
    uint32_t i;

    rb_init(&rb, mem, 4);
    for (i = 0; i < 4; i++) {
        assert(rb_put(&rb, (uint8_t)i) == 0);
    }
    assert(rb_free(&rb) == 0);
    assert(rb_put(&rb, 9) == -1);         /* 满了必须拒绝，不能覆盖未读数据 */
}

static void test_wraparound(void)
{
    rb_t rb;
    uint8_t mem[8];
    uint8_t b = 0;
    uint32_t round;
    uint32_t i;

    rb_init(&rb, mem, 8);
    for (round = 0; round < 3; round++) {
        for (i = 0; i < 5; i++) {
            assert(rb_put(&rb, (uint8_t)(round * 5 + i)) == 0);
        }
        for (i = 0; i < 5; i++) {
            assert(rb_get(&rb, &b) == 0);
            assert(b == (uint8_t)(round * 5 + i));
        }
    }
    assert(rb_used(&rb) == 0);
}

static void test_bulk(void)
{
    rb_t rb;
    uint8_t mem[8];
    uint8_t src[10];
    uint8_t dst[10];
    static const uint8_t expect[6] = {4, 5, 6, 7, 8, 9};
    uint32_t i;

    for (i = 0; i < 10; i++) {
        src[i] = (uint8_t)i;
    }
    memset(dst, 0, sizeof(dst));

    rb_init(&rb, mem, 8);
    assert(rb_write(&rb, src, 10) == 8);  /* 超出容量，截断 */
    assert(rb_used(&rb) == 8);

    assert(rb_read(&rb, dst, 4) == 4);
    assert(memcmp(dst, src, 4) == 0);

    /* 此时 tail=4，再写 2 字节必然回绕 */
    assert(rb_write(&rb, src + 8, 2) == 2);
    assert(rb_used(&rb) == 6);

    assert(rb_read(&rb, dst + 4, 6) == 6);
    assert(memcmp(dst + 4, expect, 6) == 0);
    assert(rb_used(&rb) == 0);
}

int main(void)
{
    test_init();
    test_single();
    test_full();
    test_wraparound();
    test_bulk();

    printf("ring_buffer: all tests passed\n");
    return 0;
}
