# 三块 STM32F407 环境采集、OLED 联动与 MySQL 可视化

开发者：肖肃　学号：202440700102　串口标识：XiaoSu

本实验使用三块 STM32F407 开发板。板 1 作为采集控制板，通过 SHT30 读取温湿度，通过 ADC 读取光敏、CO2 和甲醛传感器；OLED 显示本板数据。板 1 通过 CAN（500 kbit/s）把同一帧数据发送给板 2、板 3，两块接收板同步显示，并由各自的 LED 呈现呼吸灯效果。KEY1 打开联动显示和呼吸灯，KEY2 全部关闭（含 LED）；WiFi 将三块板各自的传感器数据上报 PC，写入 MySQL，并在浏览器中按板号、采集时间显示动态列表和图形。

固件入口为 `Main/main_sensor.c`，任务层位于 `Main/app_rtos.c`；PC 入口为 `server.py`，前端位于 `web/`。OLED 使用 SSD1306 I2C 驱动，不依赖 TFTLCD。

## 功能与按键

| 操作 | 行为 |
| --- | --- |
| 板 1 KEY1（PF8） | OLED 显示本板最新的温湿度、光敏、CO2、甲醛；通过 CAN 把同一帧转发到板 2/3；板 2/3 的 OLED 显示该帧，两块板各自的 LED 进入呼吸灯效果 |
| 板 1 KEY2（PF7） | 发送 `sharing=0` 的关闭状态帧；清空板 2/3 OLED 数据区并关闭全部 LED；CAN 继续周期发送关闭状态，防止接收板误判超时后恢复 |
| 板 2 KEY1 / KEY2 | 当前版本不参与联动控制；板 2 的显示与 LED 由板 1 的 KEY1/KEY2 统一控制 |
| 板 3 KEY1 / KEY2 | 当前版本不参与联动控制；板 3 的显示与 LED 由板 1 的 KEY1/KEY2 统一控制 |

板 1 的 KEY1/KEY2 均带 20 ms 消抖，长按只触发一次。OLED 的“关闭”是关闭显示（`SSD1306_DisplayOff`）并熄灭 LED，不是断开 I2C。CAN 断开或 1 秒无新帧时，板 2/3 自动清屏并关闭 LED；恢复后重新显示。板 1 的 OLED 始终显示本板实时采集值，KEY2 只停止向板 2/3 共享，不影响本地采集和 WiFi 上报。板 2/3 的板载按键在当前版本不改变联动状态，统一由板 1 控制。

三颗 LED 为 PE3、PE4、PG9，低电平点亮。呼吸灯由 TIM6 以 100 Hz 更新占空比，约 2 秒一个周期；KEY2 会关闭三路 PWM 并把引脚置为熄灭电平。

## 硬件连接

三块板使用相同传感器与 OLED 接线，板号由编译宏 `APP_BOARD_ID` 区分，不能把同一份固件烧到三块板。

| 器件/信号 | STM32F407 引脚 | 说明 |
| --- | --- | --- |
| OLED SCL / SDA | PB6 / PB7 | I2C1，SSD1306 地址通常为 0x3C |
| SHT30 SCL / SDA | PB6 / PB7 | 与 OLED 共用 I2C1；地址 0x44（7 位） |
| 光敏传感器 | PA6 | ADC1_IN6，12 位，0..4095 |
| CO2 传感器 | PA7 | ADC1_IN7，12 位；按模块模拟输出标定 |
| 甲醛传感器 | PA4 | ADC1_IN4，12 位；按模块模拟输出标定 |
| LED0 / LED1 / LED2 | PE3 / PE4 / PG9 | 低电平点亮，TIM6 PWM |
| KEY1 / KEY2 | PF8 / PF7 | 低电平按下，内部上拉 |
| CAN TX / RX | PA11 / PA12 | CAN1，500 kbit/s，收发器 3.3 V |
| ESP8266 TX / RX | PA0 / PA1 | UART4，115200，8N1；`WIFI_UART4_USE_PC10_PC11` 可切换为 PC10/PC11 |
| ESP8266 EN | PF14 | 高电平使能 |

注意：

- I2C1 总线需要 4.7 kΩ 上拉；OLED 与 SHT30 地址不能冲突。
- CAN 总线两端各接 120 Ω 终端电阻，CAN_H/CAN_L 不可反接；三块板共地。
- ESP8266 使用独立 3.3 V 电源，峰值电流需大于 300 mA；PA0 是 UART4_TX，接 ESP8266 RX，PA1 接 ESP8266 TX，交叉连接。
- ADC 输入不得超过 3.3 V；CO2/甲醛模块若为 5 V 模拟输出，必须先用分压或电平转换。

## 三块固件的编译与烧录

工程使用 Keil MDK/ARM Compiler 5。源码通过 `APP_BOARD_ID` 条件编译板号与角色，默认值在 `Main/app_config.h` 中为 1，构建脚本会显式传入 1/2/3。

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
.\.venv\Scripts\python.exe tools\build_firmware.py --board 1
.\.venv\Scripts\python.exe tools\build_firmware.py --board 2
.\.venv\Scripts\python.exe tools\build_firmware.py --board 3
```

输出 `Project/Build/Board1/Board1.hex`、`Board2.hex`、`Board3.hex`，分别烧录到对应开发板。Keil 手工构建时，在 C/C++ 预定义宏中加入 `APP_BOARD_ID=1`、`APP_BOARD_ID=2` 或 `APP_BOARD_ID=3`。板号重复时 PC 会拒绝注册。

## WiFi 与 PC 环境

PC 开启 2.4 GHz 移动热点，并在 Windows 的移动热点设置中填写自己的名称和密码；电脑热点网卡地址通常为 `192.168.137.1`，请以 `ipconfig` 为准。固件连接参数在 `Main/app_config.h`：

```c
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define WIFI_SERVER_IP "YOUR_PC_LAN_IP"
#define WIFI_SERVER_PORT 5107U
```

三块 ESP8266 都连接该热点，并各自与 PC 的 `5107` 端口建立一条 TCP 连接。PC 端需要允许防火墙入站 TCP 5107 和网页端口 8080。

启动服务（有 MySQL）：

```powershell
.\.venv\Scripts\python.exe server.py
```

无 MySQL 时仅演示通信和网页（数据不落库）：

```powershell
.\.venv\Scripts\python.exe server.py --memory-only
```

浏览器打开 `http://127.0.0.1:8080/`。

## MySQL

从 `server.example.toml` 复制一份本地 `server.toml`，填写自己的 MySQL 密码；`server.toml` 不会提交到仓库：

```toml
[mysql]
host = "127.0.0.1"
port = 3306
user = "root"
password = "YOUR_DB_PASSWORD"
database = "stm32_lab5"
```

服务启动时自动创建数据库 `stm32_lab5` 和表 `sensor_samples`；也可手工执行 `schema.sql`。表字段包括板号、TCP 会话、采样序号、板上运行毫秒、采集时间、PC 接收时间、温湿度、光敏 ADC/百分比、CO2、甲醛、有效位和共享状态。

板号与采样时间用于区分三块板：

- `collected_at`：PC 根据连接建立时的 UTC 基准和板上 `uptime_ms` 换算的采集时刻，精确到微秒；它含网络与时钟同步误差，适合本实验 2 秒级采样，不是硬件 RTC 授时。
- `received_at`：PC 实际收到该帧的 UTC 时间，用于观察网络延迟。

网页显示时统一转为浏览器本地时间。每次 TCP 重连生成新的 `session_id`，因此重连后的采样序号不会与上一会话混为一谈。

常用查询：

```sql
SELECT board_id, collected_at, received_at, temperature, humidity,
       light_adc, co2_ppm, formaldehyde_mg_m3
FROM sensor_samples
ORDER BY collected_at DESC, board_id
LIMIT 100;

SELECT * FROM sensor_samples
WHERE board_id = 2
ORDER BY collected_at DESC
LIMIT 100;
```

## 通信协议

TCP 使用 ASCII 行协议，每帧以 CRLF 结束；服务端支持粘包与拆包。每条连接首帧必须为：

```text
HELLO,板号,运行毫秒
```
