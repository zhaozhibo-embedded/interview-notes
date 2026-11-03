# 嵌入式程序的内存分段：.text / .data / .bss / heap / stack

> 说明：以 `cpu/br28/charge.c` 里的 `charge_var` 为例，变量在固件里到底放在哪、上电后是什么值。

---

## 1. 表

程序编译成固件后，变量按“存放位置”被分到不同的段：

| 段 | 存放内容 | 占 Flash | 占 RAM | 上电初值 |
|---|---|---|---|---|
| `.text` | 代码（函数） | ✅ | ❌ | — |
| `.rodata` | 常量（`const`、字符串字面量） | ✅ | ❌ | 固定值 |
| `.data` | **已初始化**的全局/静态变量（初值非 0） | ✅（存初值） | ✅ | 初值 |
| `.bss` | **未初始化或初值为 0** 的全局/静态变量 | ❌ | ✅ | 0 |
| heap | `malloc` 动态分配 | ❌ | ✅ | 随机 |
| stack | 函数局部变量、返回地址 | ❌ | ✅ | 随机 |

两个容易记错的点：

1. `.data` 是**同时占 Flash 和 RAM** 的：初值存在 Flash 里，启动时复制到 RAM；
2. `.bss` **不占 Flash，但占 RAM**。

---

## 2. 例子：每个变量落在哪

```c
int a;                          /* .bss   ：没写初值 */
int b = 0;                      /* .bss   ：初值为 0，多数编译器归到 .bss */
int c = 5;                      /* .data  ：初值非 0 */
const int d = 5;                /* .rodata：常量 */
char *s = "hello";              /* 指针在 .data，"hello" 在 .rodata */

static CHARGE_VAR charge_var;   /* .bss   ：静态全局，没写初值 */

void f(void)
{
    int e;                      /* stack  ：普通局部变量 */
    static int g;               /* .bss   ：静态局部变量，只初始化一次 */
    static int h = 1;           /* .data  ：静态局部变量，初值非 0 */
}
```

| 变量 | 段 | 原因 |
|---|---|---|
| `a` | `.bss` | 全局、无初值 |
| `b` | `.bss` | 全局、初值为 0 |
| `c` | `.data` | 全局、初值非 0 |
| `d` | `.rodata` | `const` |
| `"hello"` | `.rodata` | 字符串字面量 |
| `charge_var` | `.bss` | 静态全局、无初值 |
| `e` | stack | 函数返回就销毁 |
| `g` | `.bss` | `static` 局部、无初值 |
| `h` | `.data` | `static` 局部、初值非 0 |

---

## 3. 为什么 `.bss` 不占 Flash

`.bss` 里的变量初值**全是 0**，固件里没必要存一堆 0——存了也是浪费 Flash。

所以链接器只做两件事：

1. 在 RAM 里划出一块区域，记下“从哪到哪”；
2. 把这块区域的起始地址和大小写进启动代码要用的符号（如 `__bss_start`、`__bss_end`）。

固件文件里**没有**这块区域的任何数据。

---

## 4. 启动时谁把 `.bss` 清零

芯片上电后，启动代码（`startup` / `__main` / SDK 的 `setup_arch`）在进入 `main()` 之前会：

```
1. 把 .data 段从 Flash 复制到 RAM
2. 把 .bss 段对应的 RAM 区域整体清零
3. 初始化堆、栈
4. 跳转到 main()
```

伪代码大概是这样：

```c
extern u32 __data_start, __data_end, __data_load;
extern u32 __bss_start, __bss_end;

/* 1. .data：Flash 初值 → RAM */
memcpy(&__data_start, &__data_load, (u8 *)&__data_end - (u8 *)&__data_start);

/* 2. .bss：RAM 清零 */
memset(&__bss_start, 0, (u8 *)&__bss_end - (u8 *)&__bss_start);
```

所以程序一跑起来：

```c
charge_var.charge_online_flag = 0;
charge_var.charge_event_flag  = 0;
charge_var.init_ok            = 0;
charge_var.ldo5v_timer        = 0;
```

这也解释了前面那个问题：

> 为什么 `get_charge_online_flag()` 在 `charge_init()` 之前读也是 0？
> 因为 `charge_var` 在 `.bss`，上电已经被启动代码清零了。

---

## 5. 在 map / symbol 文件里验证

编译产物里的 `sdk.elf.resolution.txt`、`symbol_tbl.txt` 会标出每个符号所在的段。例如：

```
0010bac8 l     .bss    00000004    app_charge_wake_off
```

| 字段 | 含义 |
|---|---|
| `0010bac8` | 链接后的地址 |
| `l` | local（本文件可见） |
| `.bss` | 所在段 |
| `00000004` | 占 4 字节 RAM |
| `app_charge_wake_off` | 符号名 |

看到 `.bss` 就知道：它不占 Flash，上电为 0，占 4 字节 RAM。

---

## 6. 常见误区

| 误区 | 实际情况 |
|---|---|
| “`.bss` 不占空间” | 只不占 **Flash**，RAM 照样占 |
| “`int b = 0` 一定在 `.bss`” | 多数编译器会放 `.bss`，但 C 标准没规定，属于实现行为 |
| “`static` 变量都在 `.bss`” | `static` 只决定作用域和生命周期；放哪个段看有没有非 0 初值 |
| “`const` 一定在 Flash” | 嵌入式里通常放 `.rodata`（Flash），但局部 `const` 可能被优化进寄存器或栈 |
| “`.data` 只占 RAM” | 初值要存在 Flash 里，启动时复制到 RAM，**Flash 和 RAM 都占** |
| “栈和堆是同一块” | 通常从 RAM 两端相向生长，中间留给全局变量；具体看链接脚本 |

---

## 7. 和本工程的关系

| 变量 | 段 | 说明 |
|---|---|---|
| `charge_var`（`charge.c`） | `.bss` | 充电状态全局结构体，上电清零 |
| `app_charge_wake_off`（`init.c`，已注释） | `.bss` | 1 字节标志 |
| `_battery_level_table[]`（`product_stone.c`） | `.rodata` | `const` 数组 |
| `_chg_hist`、`_chg_online`（`product_stone.c`） | `.bss` | 充电检测的静态状态 |
| 各任务的栈 | stack | 由 `task_create` 传入的栈空间 |

---

## 8. 参考

- `cpu/br28/charge.c`：`CHARGE_VAR` / `charge_var` 定义
- `cpu/br28/tools/symbol_tbl.txt`：符号段信息
- `cpu/br28/tools/sdk.elf.resolution.txt`：链接结果
- `cpu/br28/setup.c`：启动流程
