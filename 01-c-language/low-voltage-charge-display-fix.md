# 低电压充电电量灯异常 与 电压分段启动设计说明

> 适用工程：`[JL701N]Stone_TWS`（JL7016C8 / BR28）


---

## 1. 现象

电池低于 3V 时插入充电器，出现三类异常：

1. **反复重启**：串口约每 1.3 秒重复同一段启动日志：

```
vbat: 296 × 11 次
[APP_POWER]power on low power , enter softpoweroff!
M2P_VDDIO_KEEP: 1
（复位，重新启动，重复）
```

2. **电量灯乱亮**：5 颗电量灯随机亮，先第 5 颗、再第 4 颗、再第 2 颗，无规律；
3. **低电压段不亮**：电池 2.4~2.6V 时充电指示灯完全不亮，充到 3.6V 也不亮。

---

## 2. 结论先说

| 疑问 | 结论 |
|---|---|
| 为什么插着充电器还会关机？ | `check_power_on_voltage()`（启动时的低电压关机检查）只在“充电标志为 0”时执行。启动早期这个标志**还是 0**（自研充电检测要 2.4 秒才出结果），于是照常按低电压关机。 |
| 灯为什么乱亮？ | 74HC595 的**输出锁存器会保持上次写入的值**。反复复位时 DS/SCK/RCK 浮空，噪声被打成随机数据锁存；而画灯的 `product_stone_init()` 在 `app_main()` 里，死循环根本走不到。 |
| 为什么关掉充电唤醒后一直不亮？ | 关掉 PA1 唤醒后，芯片睡下去就再也没有唤醒源，电池充到 3.6V 也不会启动显示。 |
| 怎么解决？ | **电压分段**：端电压 <2.6V 时灭灯后立刻睡、保留充电唤醒等下一个脉冲重试；≥2.6V 时提前置充电标志、正常启动进 idle 显示充电。 |

---

## 3. 根因

### 3.1 启动前的低电压检查

调用链，三个函数各自负责什么：

```
app_task_handler()            系统启动任务
  └─ app_init()               系统初始化：板级、配置、音频、电源检查
       └─ check_power_on_voltage()   启动阶段读电池电压，太低就软关机
```

`apps/soundbox/common/init.c` 的 `app_init()`：

```c
/* get_charge_online_flag()：查询“当前是否插着充电器”。
   返回 1 = 充电中（或充满），返回 0 = 没插充电器。
   ! 表示：只有“没检测到充电器”时，才做下面的低电压检查；
   插着充电器时跳过，避免把正在充电的机器关掉。 */
if (!get_charge_online_flag()) {

    /* check_power_on_voltage()：启动前的低电压关机检查。
       连续采样电池电压，连续多次低于门限就调用 power_set_soft_poweroff() 软关机，
       防止低电量强行启动，导致启动到一半掉电、反复复位。 */
    check_power_on_voltage();

    ...
}
```

`apps/soundbox/power_manage/app_power_manage.c` 的 `check_power_on_voltage()`：

```c
/* 启动阶段连续采样电池电压：
   连续 11 次低于关机门限 → 软关机；连续 11 次正常 → 启动电压监测后返回。 */
void check_power_on_voltage(void)
{
    while (1) {
        clr_wdt();                          /* 喂看门狗，避免这个循环触发硬件看门狗复位 */

        val = get_vbat_level();             /* 读电池电压，单位 0.01V：296 表示 2.96V */
        printf("vbat: %d\n", val);          /* 调试打印，每圈一次；日志里那 11 行就是它 */

        if ((val < app_var.poweroff_tone_v) /* poweroff_tone_v：低电关机门限（宏 LOW_POWER_OFF_VAL，
                                               当前为 310） */
            || adc_check_vbat_lowpower()) { /* 硬件低电标志；本工程该函数是空实现，恒返回 0 */
            low_power_cnt++;
            if (low_power_cnt > 10) {       /* 连续 11 次都低才关机，避免瞬时压降误判 */
                power_set_soft_poweroff();  /* 进入软关机（不返回），等按键或充电器边沿唤醒 */
            }
        } else {
            if (++normal_power_cnt > 10) {  /* 连续 11 次正常，认为电压稳定 */
                vbat_check_init();          /* 启动运行期的电压监测定时器 */
                return;                     /* 正常返回，继续启动 */
            }
        }
    }
}
```

涉及的关键函数/变量：

| 名称 | 作用 |
|---|---|
| `get_charge_online_flag()` | 读“充电在线”标志（RAM 里的一个字节），1 = 充电中，0 = 没充电 |
| `check_power_on_voltage()` | 启动时的低电压检查：连续 11 次低于门限就软关机 |
| `get_vbat_level()` | 读电池电压，单位 0.01V（296 = 2.96V） |
| `power_set_soft_poweroff()` | 软关机：配置唤醒源后进入 P33 深睡，不返回 |
| `vbat_check_init()` | 启动运行期的电压监测（10ms 一次，判断低电/充满/充电） |
| `app_var.poweroff_tone_v` | 低电关机门限变量。名字里的 `tone` 是 SDK 历史遗留，实际存的是电压值 |
| `adc_check_vbat_lowpower()` | 硬件低电检测接口；本工程 `adc_api.c` 里是空实现，恒返回 0 |

### 3.2 充电标志为什么还是 0

`get_charge_online_flag()` 读的是 `cpu/br28/charge.c` 里的一个 RAM 字节：

```c
void set_charge_online_flag(u8 flag)      /* 写标志：谁检测到充电状态变化，谁调用它 */
{
    __this->charge_online_flag = flag;
}

u8 get_charge_online_flag(void)           /* 读标志 */
{
    return __this->charge_online_flag;
}
```

它在 `charge_init()` 里被清 0，所以**复位后默认就是 0**，必须有代码调用 `set_charge_online_flag(1)` 才会变 1。

SDK 自带的充电检测链路（`cpu/br28/charge.c`）：

| 函数 | 作用 |
|---|---|
| `charge_init()` | SDK 充电驱动的初始化入口，注册在板级设备表里，由 `dev_manager` 在 `board_init()` 阶段调用。里面把 `charge_online_flag` 清 0，并调用一次 `check_charge_state()` |
| `check_charge_state()` | 上电时的一次性检测：最多轮询 20 次（每次 1ms），只要 `LVCMP_DET_GET()` 或 `LDO5V_DET_GET()` 为真，就把标志置 1 |
| `LVCMP_DET_GET()` | 读硬件比较器：LDOIN（充电器 5V 输入）电压是否高于 VBAT，用于判断“有没有插充电器” |
| `LDO5V_DET_GET()` | 读另一个硬件检测位：LDO5V 是否存在 |
| `ldo5v_detect()` | 运行期的定时器回调（2ms 一次），持续检测 LDO5V 的插入/拔出，并刷新标志 |

也就是说，SDK 这条链路**本来就能在启动早期把标志置 1**，前提是 LDOIN/CHGFL 按 SDK 预期接到了充电器 5V。

但这块板用的是 **IP5316**：

- 充电状态是从 **PA1 上的 LED1 波形**读出来的（0.5Hz 闪烁 = 充电中），不是 SDK 的 LDOIN/CHGFL；
- 日志证明 `check_power_on_voltage()` 确实执行了（`vbat: 296` 打印 11 次），说明检查那一刻标志仍然是 0；
- 推测是 LDOIN/CHGFL 没接到充电器 5V，或电平不在比较器门限内。**具体需实测 LDOIN 电平确认，这里不做定论。**

本工程自己的充电检测：

| 函数 | 作用 |
|---|---|
| `_product_stone_charge_scan()` | 200ms 一拍，读 PA1 电平并移入 12 位窗口（2.4 秒），判断“充电中 / 充满 / 拔线”，状态变化时调用 `set_charge_online_flag()` |
| `_product_stone_charge_fast_tick()` | 5ms 一次的硬件定时器回调，把 PA1 在这 5ms 内出现过高/低锁存下来，供上面的 200ms 扫描取用 |
| `product_stone_init()` | 在 `app_main()` 里才启动 200ms 定时器，所以这套检测最早也要 2.4 秒后才出结果 |

启动顺序决定了“标志一定是 0”：

```
app_task_handler()
  └─ app_init()
       ├─ board_init()                    ← charge_init() 把标志清 0
       ├─ if (!get_charge_online_flag())  ← 此刻标志 = 0
       │      └─ check_power_on_voltage() ← 读 11 次 vbat，低于门限就关机
       └─ 返回
  └─ app_main()                           ← 只有 app_init 没关机才会走到
       ├─ EDGE4 判断 → set_charge_online_flag(1)   ← 太晚
       └─ product_stone_init()                     ← 太晚
             └─ 200ms 定时器 → 2.4 秒后 set_charge_online_flag(1)  ← 太晚
```

**能证明“在充电”的代码都在 `app_main()` 之后，而“决定要不要关机”的代码在 `app_main()` 之前。**

### 3.3 灯为什么乱亮

74HC595 是“移位寄存器 + 输出锁存器”两级结构，锁存器只要 VCC 还在就一直保持上次写入的值，与 MCU 是否复位无关。

死循环中：

- 595 的 DS/SCK/RCK 处于浮空/复位默认态；
- 电源与充电器的瞬变在引脚上产生随机跳变；
- 595 把随机跳变当成时钟/数据，锁存出随机图案，并一直保持。

### 3.4 关掉充电唤醒的副作用

之前的临时方案是“低于 2.6V 时 `power_wakeup_index_enable(4, 0)`”。

`power_wakeup_index_enable(4, 1)` 的作用是：允许 P33 的第 4 路唤醒源（PA1，接 IP5316 的 LED1）把芯片从深睡叫醒。关掉它之后，芯片睡下去就再也没有唤醒源，**充到 3.6V 也不会亮，必须用户按键**。

---

## 4. 方案：电压分段 + 快速灭灯

### 4.1 取舍

| 方案 | 做法 | 问题 |
|---|---|---|
| A. 启动时直接跳过低电压检查 | 只要插着充电器就不检查 | 低电压时仍会反复启动，灯乱亮 |
| B. 低电压时关闭充电唤醒 | `power_wakeup_index_enable(4, 0)` | 睡死后再也醒不过来 |
| C. 改门限 | 调整 `poweroff_tone_v` | 只是把循环点挪个位置，不解决显示 |
| **D. 电压分段 + 快速灭灯（采用）** | <2.6V 灭灯后睡、≥2.6V 正常启动 | 需新增灭灯函数与分段判断 |

### 4.2 改动点

1. `apps/soundbox/common/init.c`
   - 充电器边沿唤醒时按电压分段：`<260` 灭灯 + 软关机；`≥260` 置充电标志。
2. `apps/common/coolwal/product_stone.c/.h`
   - 新增 `product_stone_early_leds_off()`，只驱动 595 输出全灭。
3. `apps/soundbox/board/br28/board_jl701n_demo/board_jl701n_demo.c`
   - 软关机与普通 powerdown 两处恢复固定使能 PA1 充电唤醒。

### 4.3 关键代码

`init.c`：

```c
/* is_wakeup_source(PWR_WK_REASON_EDGE_INDEX4)：
   查询本次启动是不是“充电器 LED1 边沿”唤醒的。
   P33 的第 4 路唤醒源就是 PA1，接 IP5316 的 LED1。 */
if (is_wakeup_source(PWR_WK_REASON_EDGE_INDEX4)) {

    /* get_vbat_level()：读电池端电压，单位 0.01V；260 = 2.60V */
    if (get_vbat_level() >= 260) {
        /* 2.6V 以上：提前把“充电在线”标志置 1，
           让后面 if (!get_charge_online_flag()) 不成立，
           从而跳过 check_power_on_voltage()，不会被 3.20V 门限关机。 */
        set_charge_online_flag(1);
    } else {
        /* 2.6V 以下：先灭灯，再立刻睡回去。
           这里不关 PA1 唤醒，充电器下一个 LED 脉冲会把芯片再叫醒，
           等端电压升到 2.6V 以上，就会自动走上面那条分支启动显示。 */
        product_stone_early_leds_off();
        power_set_soft_poweroff();       /* 软关机，不返回 */
    }
}
```

`product_stone.c`：

```c
/* 极低电压快速灭灯：只初始化 595 并输出全灭（保留位 6），
   不初始化其它外设。供 app_init 在低电压软关机前调用。 */
void product_stone_early_leds_off(void)
{
    ic_74hc595_init(true);       /* 把 PA07/PA02/PA06 配成输出，清空影子寄存器；true = 低电平点亮取反 */
    ic_74hc595_clear();          /* 影子寄存器 8 位全部清 0 */
    ic_74hc595_set(6, true);     /* 位 6 是氛围灯，保持和正常关机一样的灯态 */
    ic_74hc595_update();         /* 把 8 位移位输出并锁存，真正改变 LED */
}
```

`board_jl701n_demo.c`：

```c
power_wakeup_index_enable(4, 1);   /* board_set_soft_poweroff()：软关机前使能 PA1 充电唤醒 */
power_wakeup_index_enable(4, 1);   /* sleep_enter_callback()：普通 powerdown 前同样使能 */
```

### 4.4 改后时序

| 唤醒时端电压 | 程序行为 | 显示 |
|---|---|---|
| < 2.60V | 灭灯 → 软关机 → 等下一个充电脉冲 | 全灭（不随机亮） |
| 2.60 ~ 3.09V | 启动 → idle → 等充电检测稳定 | 约 2.4 秒后第 1 颗 0.5Hz 闪 |
| 3.10 ~ 3.49V | 启动 → idle | 立即显示第 1 颗常亮，约 2.4 秒后转闪 |
| ≥ 3.50V | 正常启动 | 按档位显示 N 格，约 2.4 秒后转充电闪 |

> 2.60~3.09V 延迟约 2.4 秒，是因为“低于 3.10V 全灭”的规则要等充电检测确认 `_chg_online = 1` 后才解除。

---

## 5. 关键知识点

- **74HC595 两级结构**：移位寄存器负责“串行装入”，输出锁存器负责“并行输出”。`SCK` 上升沿移入一位，`RCK` 上升沿把 8 位同时送到 Q0~Q7。移位过程中 LED 不变，因此不会闪烁。
- **锁存器保持性**：只要 VCC 在，锁存器就保持上次写入的值，MCU 复位、引脚浮空都不影响。这是“写一次全灭能一直保持”的原因，也是“噪声写入后会一直乱亮”的原因。
- **低电平点亮**：板上 LED 是 595 输出 0 点亮、1 熄灭，所以 `ic_74hc595_init(true)` 取反，上层逻辑仍用 `true = 亮`。
- **位映射**：位 5~位 1 = 电量灯 1~5，位 6 = 氛围灯（可选），位 7/位 0 未用。
- **端电压 ≠ 开路电压**：`get_vbat_level()` 读的是电池端电压。插着充电器时端电压会被抬高，所以 2.4V 的电池实际可能读到 2.7~2.9V。
- **PA1 / index4**：充电器 LED 接 `IO_PORTA_01`，配置为 P33 第 4 路唤醒源；`power_wakeup_index_enable(4, 1)` 打开，`sleep_exit_callback()` 里关闭，防止充电脉冲持续触发中断。
- **`check_power_on_voltage()`**：启动前连续读 11 次电压，低于 `poweroff_tone_v` 就软关机；只有 `get_charge_online_flag()` 为 0 时才会调用。
- **充电检测窗口**：`_product_stone_charge_scan()` 每 200ms 记一拍，12 拍滑动窗口（2.4 秒）判断“有高有低 = 充电中 / 恒高 = 充满 / 恒低 = 拔线”。
- **芯片电压范围（手册）**：`VBAT 2.2~4.5V`、`IOVDD 2.2~3.6V`、`VPWR 4.5~5.5V`。这是芯片能工作的范围，不代表整机在 2.2V 能带 BT + 功放满负载。

---

## 6. 测试用例

| 场景 | 预期 |
|---|---|
| 电池 2.4V 插充电器 | 电量灯全灭，无随机乱亮；约每秒一次轻微唤醒 |
| 电池充到 2.65V（不按键） | 第 1 颗电量灯自动开始 0.5Hz 闪 |
| 电池 2.8V 插充电器 | 启动一次进 idle，第 1 颗闪，无重启 |
| 电池 3.3V 插充电器 | 正常充电显示，板子键可正常开机 |
| 2.8V 充电中拔掉充电器 | 低电关机，不进入循环 |
| 2.8V 长按板子键 | 不开机（3.10V 门限） |
| 冷启动（装电池不按键） | 正常进 idle，无额外等待 |

---

## 7. 风险与注意事项

1. **2.4~2.6V 期间约每秒唤醒一次**，每次 200~300ms；平均电流几 mA，充电器可带。开调试时串口会反复打印启动日志，**量产固件应关闭 `CONFIG_DEBUG_ENABLE`**。
2. **端电压低于芯片最低工作电压时**，部分唤醒会直接 LVD 复位、代码跑不到 `app_init`；此时灯保持全灭，靠 IP5316 继续涓流充电，电压上来后自动恢复。
3. **IP5316 预充阶段的 LED 行为待实测**：若 2.6~3.0V 时 LED 不按 0.5Hz 闪，自研检测会判“没充电”，可能导致灯不亮或 1~2 秒循环。需要打开 `[CHG]` 打印确认 `hist/online`。
4. **充电中低电保护被屏蔽**：`vbat_check()` 的低电关机整段在 `if (get_charge_online_flag() == 0)` 里，充电中不会因低电关机。若充电器太弱、带不动整机，只能靠电池保护板兜底。
5. **代码清理**：`init.c` 中已注释的 `app_charge_wake_off` 全局与旧分支、`board_jl701n_demo.c` 中注释掉的门控代码，提交前删除。

---

## 8. 参考

- `apps/soundbox/common/init.c`：`app_init()` / `check_power_on_voltage()` 调用点
- `apps/soundbox/power_manage/app_power_manage.c`：`check_power_on_voltage()` / `vbat_check()`
- `cpu/br28/charge.c`：`charge_init()` / `check_charge_state()` / `ldo5v_detect()` / `set_charge_online_flag()`
- `cpu/br28/adc_api.c`：`adc_check_vbat_lowpower()`（空实现）
- `apps/common/coolwal/product_stone.c`：`product_stone_early_leds_off()` / `_product_stone_charge_scan()` / `_product_stone_charge_fast_tick()`
- `apps/common/coolwal/ic_74hc595.c`：595 驱动
- `apps/soundbox/board/br28/board_jl701n_demo/board_jl701n_demo.c`：`board_set_soft_poweroff()` / `sleep_enter_callback()`
