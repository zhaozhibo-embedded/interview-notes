# 手写代码

> 面试经常让你现场写。这里放的实现要求：**能编译、能跑、有边界测试。**

## 篇目

| 文件 | 内容 | 被考频率 |
|---|---|---|
| [ring_buffer/](ring_buffer/) | 环形缓冲区（单生产者单消费者，无锁） | ★★★★★ |
| 待补 | 按键消抖（状态机版） | ★★★★ |
| 待补 | 状态机框架（表驱动） | ★★★★ |
| 待补 | CRC16/CRC32（查表 + 位运算） | ★★★ |
| 待补 | 字符串/链表反转 | ★★★ |

## 编译与测试

```bash
cd ring_buffer
gcc -Wall -Wextra -O2 -std=c99 ring_buffer.c test_ring_buffer.c -o test_rb
./test_rb
```

Windows 下需要 MinGW-w64 或 MSYS2 的 gcc。

> 提交前必须自己跑通。面试官会 clone 下来跑的。
