#include "ring_buffer.h"

#include <string.h>

static int is_power_of_two(uint32_t v)
{
    return (v != 0u) && ((v & (v - 1u)) == 0u);
}

int rb_init(rb_t *rb, uint8_t *storage, uint32_t size)
{
    if (rb == NULL || storage == NULL || !is_power_of_two(size)) {
        return -1;
    }
    rb->buf  = storage;
    rb->size = size;
    rb->head = 0u;
    rb->tail = 0u;
    return 0;
}

void rb_reset(rb_t *rb)
{
    if (rb == NULL) {
        return;
    }
    rb->head = 0u;
    rb->tail = 0u;
}

uint32_t rb_used(const rb_t *rb)
{
    /* 无符号回绕后差值仍然正确 */
    return rb->head - rb->tail;
}

uint32_t rb_free(const rb_t *rb)
{
    return rb->size - rb_used(rb);
}

int rb_put(rb_t *rb, uint8_t byte)
{
    if (rb == NULL || rb_free(rb) == 0u) {
        return -1;
    }
    rb->buf[rb->head & (rb->size - 1u)] = byte;
    rb->head++;
    return 0;
}

int rb_get(rb_t *rb, uint8_t *byte)
{
    if (rb == NULL || byte == NULL || rb_used(rb) == 0u) {
        return -1;
    }
    *byte = rb->buf[rb->tail & (rb->size - 1u)];
    rb->tail++;
    return 0;
}

uint32_t rb_write(rb_t *rb, const uint8_t *src, uint32_t len)
{
    uint32_t space;
    uint32_t idx;
    uint32_t first;

    if (rb == NULL || src == NULL || len == 0u) {
        return 0u;
    }

    space = rb_free(rb);
    if (len > space) {
        len = space;
    }
    if (len == 0u) {
        return 0u;
    }

    idx   = rb->head & (rb->size - 1u);
    first = rb->size - idx;      /* 到缓冲区末尾的连续空间 */
    if (first > len) {
        first = len;
    }

    memcpy(&rb->buf[idx], src, first);
    if (len > first) {
        memcpy(&rb->buf[0], src + first, len - first);  /* 回绕部分 */
    }

    rb->head += len;
    return len;
}

uint32_t rb_read(rb_t *rb, uint8_t *dst, uint32_t len)
{
    uint32_t avail;
    uint32_t idx;
    uint32_t first;

    if (rb == NULL || dst == NULL || len == 0u) {
        return 0u;
    }

    avail = rb_used(rb);
    if (len > avail) {
        len = avail;
    }
    if (len == 0u) {
        return 0u;
    }

    idx   = rb->tail & (rb->size - 1u);
    first = rb->size - idx;
    if (first > len) {
        first = len;
    }

    memcpy(dst, &rb->buf[idx], first);
    if (len > first) {
        memcpy(dst + first, &rb->buf[0], len - first);
    }

    rb->tail += len;
    return len;
}
