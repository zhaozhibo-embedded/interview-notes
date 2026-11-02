# 真待机「单击误开机」问题分析与修复记录

> 工程：JL701N / BR28 Stone TWS 音箱（`[JL701N]Stone_TWS - 当前使用代码 - Deepseek`）
> 现象：真待机（4 击关机）状态下，**单击**一次板子开关机键会直接开机；开串口调试不复现，关调试必现。
> 目标：真待机只响应**长按**开机，时长与伪待机（约 2.2 秒）保持一致。

---

## 1. 现象

1. 长按开关机键关机 → 伪待机（遥控/板子键都能开机）；
2. 板子键 **4 击** → 真待机（`power_set_soft_poweroff()`，0.3mA左右）；
3. 在真待机下**点一下**开关机键（约 100~300ms）→ 设备直接开机，而不是保持真待机；
4. 打开串口调试（`CONFIG_DEBUG_ENABLE`）时**不会**复现；
5. 关闭串口调试后**必现**；
6. 早期只有真待机一条路径的工程，同样操作不会直接开机。

---

## 2. 根因

### 2.1 真待机的唤醒与开机路径

真待机时芯片处于软关机，开关机键 PC02 配置为唤醒口（`wk_param.port[1] = &port0`，下降沿）：

```
按下按键（下降沿）
  → P33 检测到边沿，唤醒复位
  → 重新走完整启动流程
  → app_main() 判断"这次是开机还是待机"
```

`app_main()` 的判断是（改动前）：

```c
extern u8 get_power_on_status(void);
if (get_power_on_status()) {
    app_curr_task = APP_POWERON_TASK;   // SW1正按住:正常开机
}
else if (is_wakeup_source(PWR_WK_REASON_PORT_EDGE)) {
    app_short_key_wake = 1;             // 键已松开 = 单击
    app_curr_task = APP_IDLE_TASK;
}
else {
    app_curr_task = APP_IDLE_TASK;
}
```

而 `get_power_on_status()` 的实现（`board_jl701n_demo.c:1048`）只是**读一次 GPIO 电平**：

```c
u8 get_power_on_status(void)
{
    if (gpio_read(power_io_list->key_type.one_io.port) == power_io_list->connect_way)
        return 1;      // 此刻按着
    return 0;          // 此刻松着
}
```

**它回答的是"此刻是否按着"，不是"按了多久"。**

### 2.2 为什么单次采样会误判

按键唤醒发生在**按下**的瞬间，芯片随即复位启动，而手指可能还按着，也可能已经松开。

```
按下 ──────────── 松开
        ↑              ↑
   采样点可能在这里   也可能在这里
```

| 采样点位置 | `get_power_on_status()` | 走向 | 结果 |
|---|---|---|---|
| 松手**之后** | 0 | `else if` 唤醒源分支 | 判为单击 → 回真待机（正常） |
| 松手**之前** | 1 | 第一个 `if` | 判为按住 → **直接开机**（异常） |

是否误判，取决于两个时间的相对大小：

```
启动到采样点耗时  <  按下时长  →  误判为开机
启动到采样点耗时  >  按下时长  →  正确判为单击
```

### 2.3 为什么调试开关会改变结果

调试串口是**阻塞式**发送（`cpu/br28/uart_dev.c:91`）：

```c
static void UT0_putbyte(char a)
{
    JL_UART0->BUF = a;
    while ((JL_UART0->CON0 & BIT(15)) == 0);   // 忙等，直到发完
}
```

波特率 460800，一个字符约 22µs；再加上 `printf` 的格式化开销（进制转换、除法），以及启动阶段上百行日志（SDFILE、USER_CFG、ADC、音频初始化等），启动到 `app_main` 的耗时相差约 **100~300ms**：

| 调试 | 启动到 `app_main` 的耗时（量级） | 单击（100~300ms）在采样时 |
|---|---|---|
| 打开 | 约 300~370ms | 通常已松手 → 返回 0 |
| 关闭 | 约 150~250ms | 可能还按着 → 返回 1 |

**不是调试开关有魔法，而是它把采样点挪到了"单击还没结束"的位置。**

### 2.4 之前的修复为什么没覆盖

此前修过"真待机单击回伪待机"，加的是 `app_short_key_wake` + `is_wakeup_source(PWR_WK_REASON_PORT_EDGE)` 分支。

但这段代码在 `else if` 里——**只有第一个 `if` 不成立时才会执行**：

```c
if (get_power_on_status()) {          // 采样时还按着 → 走这里，直接开机
    app_curr_task = APP_POWERON_TASK;
}
else if (is_wakeup_source(...)) {     // 采样时已松手 → 才走这里
    app_short_key_wake = 1;
}
```

也就是说：**上一次修复只覆盖了"采样时已松手"的时序窗口，"采样时仍按着"的窗口没有覆盖。**

### 2.5 为什么老工程没有暴露

对比 `[JL701N]Stone_TWS - 当前使用代码 - 改同步关机前代码` 与 `[JL701N]Stone_TWS - 当前使用代码`：

| 项目 | 开机判定 | `TCFG_POWER_ON_NEED_KEY` | 唤醒口 | 唤醒表 |
|---|---|---|---|---|
| 改同步关机前代码 | 单次采样 `get_power_on_status()` | 0 | `port0` = PC02，下降沿 | `wk_param.port[1] = &port0` |
| 当前使用代码 | 同上 | 0 | 同上 | 同上 |
| 本工程 | 单次采样 + 唤醒源分支 | 0 | 同上 | 同上 |

**三份工程的决策代码和唤醒配置完全一样。**

老工程之所以"看起来没问题"，是因为它启动更慢（固件更大、日志更多、初始化顺序不同），采样点总是落在松手之后——**不是逻辑更正确，而是没撞上这个竞态**。

一旦关掉调试、启动变快，同样的隐患就会暴露。

### 2.6 SDK 里本来就有正确实现，但被关掉了

`apps/soundbox/common/init.c:68` 有一个 `check_power_on_key()`：

```c
static void check_power_on_key(void)
{
    while (1) {
        clr_wdt();
        os_time_dly(1);
        if (get_power_on_status()) {
            if (++delay_10ms_cnt > 70) return;      // 按住 700ms → 继续开机
        } else {
            if (++delay_10msp_cnt > 20) {           // 松开 200ms → 睡回去
                power_set_soft_poweroff();
            }
        }
    }
}
```

这正是"测量按住时长"的实现，但被 `TCFG_POWER_ON_NEED_KEY` 关掉了：

```c
// apps/soundbox/include/app_config.h:142
#undef TCFG_POWER_ON_NEED_KEY
#define TCFG_POWER_ON_NEED_KEY    0
```

而且它的调用条件里还有 `cpu_reset_by_soft()` / 闹钟唤醒等例外分支，直接打开需要额外确认这些分支不会跳过检查，因此本次没有采用它。

---

## 3. 方案取舍

| 方案 | 做法 | 优点 | 缺点 | 结论 |
|---|---|---|---|---|
| A. 测量按住时长 | `app_main` 里观察按键持续时间 | 与启动速度无关；只改一处；可控 | 按住开机多等约 2 秒 | **采用** |
| B. 打开 `TCFG_POWER_ON_NEED_KEY` | 启用 SDK 的 `check_power_on_key()`（700ms/200ms） | 代码更"正统" | 有 `cpu_reset_by_soft()` 等例外分支，需逐一确认 | 暂不采用 |
| C. 改成单击开机 | 单击即开机 | 识别率高、响应快 | 误触风险大；与板子键、伪待机行为不一致 | 放弃 |
| D. 维持单次采样 | 不动 | 无改动 | 依赖启动速度，换配置/换版本就会漂移 | 放弃 |

---

## 4. 实现

### 4.1 代码对比

**改前（`apps/soundbox/app_main.c`）：**

```c
extern u8 get_power_on_status(void); 
if (get_power_on_status()) {
    app_curr_task = APP_POWERON_TASK;   // SW1正按住:正常开机
}
else if(is_wakeup_source(PWR_WK_REASON_PORT_EDGE)){
    /* 是被 SW1 的边沿从真待机唤醒的，但键已经松开 = 只是点了一下。 */
    printf("[APP] short key wake -> back to real standby\n");
    app_short_key_wake = 1;
    app_curr_task = APP_IDLE_TASK;
} 
else {
    app_curr_task = APP_IDLE_TASK;      // 没按键:上电待机→idle里软关机(0.1mA)
}
```

**改后：**

```c
extern u8 get_power_on_status(void); 
if (get_power_on_status()) {
    /* ★和伪待机保持一致：按住约 2 秒才算"真要开机"。
       app_main 本身已经在按下后 0.2~0.3 秒才执行，
       这里再观察 2000ms，总按住时长 ≈ 2.2~2.3 秒。 */
    u32 key_t0 = timer_get_ms();
    while (get_power_on_status() &&
           (u32)(timer_get_ms() - key_t0) < 2000) {
        os_time_dly(2);                 /* 20ms 查一次 */
    }

    if (get_power_on_status()) {
        app_curr_task = APP_POWERON_TASK;    /* 按住够久：正常开机 */
    } else {
        printf("[APP] short key wake -> back to real standby\n");
        app_short_key_wake = 1;              /* 中途松手：单击 */
        app_curr_task = APP_IDLE_TASK;
    }
}
else if(is_wakeup_source(PWR_WK_REASON_PORT_EDGE)){
    /* 采样时已经松手，但确实是被 SW1 的边沿唤醒的 = 单击 */
    printf("[APP] short key wake -> back to real standby\n");
    app_short_key_wake = 1;
    app_curr_task = APP_IDLE_TASK;
} 
else {
    app_curr_task = APP_IDLE_TASK;
}
```

### 4.2 逐行说明

| 代码 | 作用 |
|---|---|
| `u32 key_t0 = timer_get_ms();` | 记录观察起点（毫秒）。`u32` 与 `timer_get_ms()` 返回类型一致。 |
| `while (get_power_on_status() && (u32)(timer_get_ms() - key_t0) < 2000)` | 按键仍按着、且未满 2000ms 时继续观察。`(u32)` 强转保证约 49.7 天回绕时减法仍然正确。 |
| `os_time_dly(2);` | 每 20ms 查一次，并主动让出 CPU，避免阻塞其它任务。 |
| 循环后的 `if (get_power_on_status())` | 区分两种退出原因：按满 2 秒（还按着）→ 开机；中途松手（已松开）→ 单击。 |
| `app_short_key_wake = 1;` | 通知 idle 任务：本次是被单击唤醒的，立即回真待机。 |
| `APP_IDLE_TASK` | 进入 idle；`app_idle_task()` 开头检测到该标志后执行 `product_stone_suspend()` + `power_set_soft_poweroff()`。 |
| 保留的 `is_wakeup_source` 分支 | 覆盖"采样时按键已经松开"的时序（上一次修复覆盖的场景）。 |

### 4.3 为什么阈值取 2000ms

伪待机的开机时长由 `idle.c` 的 `POWER_ON_CNT` 决定：

```
0.75 秒（AD 键 long_time = 75 × 10ms）
+ 10 次 HOLD × 150ms
= 约 2.25 秒
```

真待机路径在 `app_main` 执行前已经有约 0.25 秒的启动时间，因此这里再等 2000ms：

```
0.25 秒（启动） + 2.0 秒（观察） ≈ 2.25 秒
```

两条路径的开机时长基本一致，用户手感统一。

| 场景 | 按下 → 开机音开始 |
|---|---|
| 伪待机 + 板子键长按 | 约 2.25 秒 + 提示音启动 |
| 真待机 + 板子键长按（改后） | 约 2.25 秒 + 提示音启动 |
| 真待机 + 单击 | 判为单击 → 静默回真待机 |
| 遥控开机（伪待机） | 约 0.4 秒（未改动） |

---

## 5. 行为对照

| 场景 | 改前 | 改后 |
|---|---|---|
| 真待机 · 单击（100~300ms） | 可能直接开机 | 保持真待机 |
| 真待机 · 按住 1s | 可能直接开机 | 保持真待机 |
| 真待机 · 按住 2.5s | 开机 | 开机 |
| 伪待机 · 按住 2.5s | 开机 | 开机（未改动） |
| 冷启动 / 充电唤醒 | 进 idle | 进 idle（未改动） |
| 调试开 / 关 | 行为不一致 | 行为一致 |

---

## 6. 验证

环境：真待机由板子键 4 击进入；`CONFIG_DEBUG_ENABLE` 分别打开/关闭各测一遍。

| # | 操作 | 期望结果 | 实测 |
|---|---|---|---|
| 1 | 关闭调试，4 击进真待机 | 正常进入真待机 | ✅ |
| 2 | 单击开关机键（<0.5s） | 无反应，保持真待机 | ✅ |
| 3 | 按住约 1s 后松手 | 无反应，保持真待机 | ✅ |
| 4 | 按住 2.5s 以上 | 正常开机 | ✅ |
| 5 | 伪待机下按住 2.5s | 正常开机（与改前一致） | ✅ |
| 6 | 装电池（不按键） | 正常进入 idle，无额外等待 | ✅ |
| 7 | 插充电器唤醒 | 正常进入 idle 并显示充电 | ✅ |
| 8 | 打开调试重复 2~5 | 行为与关闭调试时一致 | ✅ |

---

## 7. 影响范围与风险

### 7.1 影响范围

- 仅影响"**被开关机键从真待机唤醒**"这一条路径；
- 冷启动（装电池不按键）、充电唤醒、闹钟唤醒不受影响；
- 伪待机开机路径、遥控开机路径均未改动；
- 与调试开关无关，开/关调试行为一致。

### 7.2 已知代价

1. **按住开机需要约 2.2~2.3 秒**才能听到开机提示音，比改前（约 0.4 秒）慢；
2. **按住不足 2 秒会判为单击**，设备静默回真待机。用户如果习惯"短按开机"，需要适应；
3. 每次被拒绝的按键会完整启动一次（约 0.25 秒）再睡回去，存在少量无效功耗。

### 7.3 可调参数

- 观察时长 `2000`（`app_main.c`）可调整：
  - 改小（如 700~1000ms）：开机更快，但防误触余量变小；
  - 改大（如 2500ms）：更防误触，但开机更慢；
- 调整时建议同时核对伪待机的 `POWER_ON_CNT`，保持两条路径手感一致。

---

## 8. 遗留与后续

1. **统一两条开机路径**：若希望彻底一致，可让真待机唤醒后直接进入 idle，复用 `idle_key_poweron_deal()` 的 `KEY_POWER_ON` / `POWER_ON_CNT` 计数逻辑，而不是在 `app_main` 里重复实现计时。
2. **SDK 自带实现**：`TCFG_POWER_ON_NEED_KEY` + `check_power_on_key()` 是更"正统"的做法，但需要先确认 `cpu_reset_by_soft()`（软复位）和闹钟唤醒分支不会跳过检查。
3. **调试打印对 433 解码的影响**（另一个独立问题）：TWS 搜索时协议栈持续输出 `P/C` 调试字符，串口忙等会挤占 CPU 和中断响应，导致 433 遥控解码偶发丢帧。建议量产固件关闭调试输出，或后续把 433 解码从"数中断"改为"读硬件定时器计数器"。
4. **代码清理**：`app_main.c` 中改动时留下的注释掉的旧代码（`// extern u8 get_power_on_status(void); ...`）应在提交前删除。
