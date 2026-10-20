# volatile 到底保证什么

## 一句话结论

`volatile` 只保证**每次访问都真的去内存/寄存器读一次、写一次**，不保证原子性，也不保证多核之间的可见顺序。

## 为什么

编译器在 `-O2` 下会把变量缓存进寄存器，并做"这个循环里没人改它"的推理。对普通变量这是对的，但对**可能被本线程之外的东西修改**的变量就是灾难：

| 场景 | 谁在偷偷改它 |
|---|---|
| 中断服务函数里的标志位 | ISR |
| 内存映射寄存器（如 `*(volatile uint32_t *)0x40021000`） | 硬件 |
| DMA 目标缓冲区 | DMA 控制器 |
| 多核共享内存 | 另一个核 |

这三种情况下，编译器"看不到"修改，就会把读操作优化掉。

## 反例与验证

```c
static int g_flag = 0;              /* 去掉 volatile */

void isr(void) { g_flag = 1; }

void wait_ready(void)
{
    while (g_flag == 0) { }         /* -O2 下可能永远出不来 */
}
```

`arm-none-eabi-gcc -O2 -S` 的典型结果：循环体里再也没有 `ldr`，只剩一条无条件跳转——编译器认为 `g_flag` 在循环内不会变。

加上 `volatile` 后，汇编里每轮循环都会重新 `ldr`。

**验证方法**：把 `-S` 输出贴到 [godbolt.org](https://godbolt.org)（选 `ARM GCC`），对比加与不加的汇编差异，截图放 `assets/`。

## 常见误区

1. **`volatile` 不是原子操作。** `g_cnt++` 是读-改-写三步，中断打断照样丢数据；要原子性用关中断、`__atomic` 或互斥量。
2. **`volatile` 不是内存屏障。** Cortex-M 上多核/外设顺序，需要 `__DMB()` / `__DSB()`。
3. **不要滥用。** 给所有变量加 `volatile` 会阻止优化，还掩盖真正的并发 bug。

## 面试会怎么追问

| 追问 | 回答要点 |
|---|---|
| `volatile` 和 `const` 能一起用吗？ | 能。`const volatile uint32_t *REG` 表示"程序不写、硬件会改"的只读状态寄存器，非常常见 |
| `volatile uint32_t *p` 和 `uint32_t * volatile p` 区别？ | 前者指向的内容是 volatile，后者指针本身是 volatile |
| `volatile` 能替代锁吗？ | 不能。只解决"不被优化掉"，不解决竞态 |

## 参考

- 《C 陷阱与缺陷》/ MISRA C:2012 Rule 8.13
- Cortex-M3/M4 权威指南，第 7 章存储器屏障
