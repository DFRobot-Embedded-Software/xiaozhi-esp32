# DFRobot FireBeetle 2 ESP32-P4 AI 交互开发板

对应产品：[FireBeetle 2 ESP32-P4 AI Development Kit (DFR1237)](https://www.dfrobot.com/product-2950.html)

固件名称：`df-p4_ai_interaction_development_board-p4x`（适配 ESP32-P4 **v3.x** 芯片，如 v3.2）

> **注意**：请勿直接修改根目录下的 `sdkconfig`（该文件已被 git 忽略，且无法共享给他人）。芯片版本与 ESP-Hosted 引脚配置已写入本目录的 `config.json` 与 `sdkconfig.defaults`。

## 硬件说明

- 主控：ESP32-P4（360MHz 双核 RISC-V）
- Wi-Fi / 蓝牙：板载 ESP32-C6，经 SDIO 与 P4 通信（ESP-Hosted），C6 复位引脚 GPIO5
- 音频：ES8388（I2C: GPIO7/8，I2S: MCLK=30, BCLK=29, WS=27, DOUT=26, DIN=28）
- 按键：BOOT（GPIO35）
- LED：GPIO3
- 显示屏：TL043WVV02-B1900A（480×800，2-lane @ 1000 Mbps，DPI 28MHz，vendor init）
  - 参考 [DFRobot BOARD_DFROBOT_FIREBEETLE_ESP32_P4_LCD_4_3.h](https://github.com/cdjq/ESP32_Display_Panel/blob/master/src/board/supported/dfrobot/BOARD_DFROBOT_FIREBEETLE_ESP32_P4_LCD_4_3.h)
  - LCD RST：**GPIO21**（低电平有效）
  - 背光：**GPIO11**（高电平点亮，PWM 调光）
  - MIPI PHY LDO：CH3 / 2.5V

MIPI-CSI 摄像头需外接树莓派兼容模块，当前固件未包含摄像头驱动。

## 编译配置

### 方式一：release 脚本（推荐）

```bash
python scripts/release.py df-p4_ai_interaction_development_board
```

### 方式二：手动 idf.py 编译

首次配置或切换板型后，请删除旧 `sdkconfig` 并加载本板默认配置：

```bash
idf.py set-target esp32p4
del sdkconfig
idf.py -DSDKCONFIG_DEFAULTS="main/boards/df-p4_ai_interaction_development_board/sdkconfig.defaults;sdkconfig.defaults;sdkconfig.defaults.esp32p4" reconfigure
idf.py menuconfig
```

在 menuconfig 中选择：

```
Xiaozhi Assistant -> Board Type -> DFRobot FireBeetle 2 ESP32-P4 AI交互开发板
```

确认 **Component config → Hardware Settings → Chip revision** 中：
- 未勾选 “revision less than v3.0”
- Minimum revision 为 **v3.1** 或更高

然后编译烧录：

```bash
idf.py build flash monitor
```

若烧录报错 `requires chip revision in range [v1.0 - v1.99]`，说明仍在使用 v1.x 配置，请删除 `sdkconfig` 后按上述步骤重新配置。
