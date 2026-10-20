#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stddef.h>

/*
 * 单生产者 / 单消费者环形缓冲区（SPSC）。
 *
 * 约束：
 *   - size 必须是 2 的幂，用掩码代替取模
 *   - head 只由生产者写，tail 只由消费者写
 *   - 用于中断与任务之间传数据时，SPSC 场景下无需关中断
 */
typedef struct {
    uint8_t *buf;
    uint32_t size;          /* 必须是 2 的幂 */
    volatile uint32_t head; /* 写索引，只由生产者修改 */
    volatile uint32_t tail; /* 读索引，只由消费者修改 */
} rb_t;

/* 返回 0 成功，-1 参数非法（size 非 2 的幂） */
int rb_init(rb_t *rb, uint8_t *storage, uint32_t size);

void rb_reset(rb_t *rb);

/* 已用 / 剩余空间，单位字节 */
uint32_t rb_used(const rb_t *rb);
uint32_t rb_free(const rb_t *rb);

/* 单字节：返回 0 成功，-1 满/空 */
int rb_put(rb_t *rb, uint8_t byte);
int rb_get(rb_t *rb, uint8_t *byte);

/* 批量：返回实际写入/读出的字节数（可能小于 len） */
uint32_t rb_write(rb_t *rb, const uint8_t *src, uint32_t len);
uint32_t rb_read(rb_t *rb, uint8_t *dst, uint32_t len);

#endif /* RING_BUFFER_H */
