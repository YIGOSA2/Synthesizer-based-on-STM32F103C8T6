# STM32 Audio Synthesizer (STM32F103)

基于 STM32F103 的嵌入式音频合成器项目，支持 PWM 音频输出、4 声部复音、MIDI 输入、Tremolo/Delay 效果、OLED 实时 UI，并采用 FreeRTOS 实现“中断硬实时 + 任务软实时”的分层架构。

## Features

- 22.05kHz 音频采样（TIM2 中断驱动）
- 12-bit PWM 音频输出（TIM3 CH1）
- 4 声部复音（含 voice stealing 抢占策略）
- DDS + 波表振荡器（256 点查表）
- AR 包络（Attack/Release）
- Tremolo（5Hz LFO）
- Delay（约 150ms，含反馈、低通与噪声门）
- MIDI IN（USART1 RX, 31250 8N1，支持 Running Status）
- 本地按键输入 + OLED 状态显示
- FreeRTOS 调度（输入与 UI 在任务层执行）

## Hardware

- MCU: STM32F103 @ 72MHz
- 开发环境: Keil MDK-ARM
- 显示: OLED (I2C)
- 音频: PWM 输出
- MIDI: USART1 RX (PA10)
- 按键: 本地音符键 + 效果键（Tremolo/Delay/波形切换）

## Software Architecture

- **ISR（硬实时）**
  - `TIM2_IRQHandler`: 每采样周期调用 `synth_get_sample()` 并写入 PWM 比较值
  - `USART1_IRQHandler`: MIDI 字节接收并入环形缓冲
- **Task（软实时）**
  - `defaultTask` 周期执行 `input_task_step()` 与 `oled_ui_task_step()`
- **Synthesis Core**
  - Voice 管理、包络推进、混音、Tremolo、软限幅、Delay

## Signal Path

1. 输入源：本地按键 / MIDI Note On-Off
2. 合成：DDS 振荡器 + AR 包络 + 复音混音
3. 效果：Tremolo -> 软限幅 -> Delay -> 软限幅
4. 输出：映射到 `[0, 4095]` 并写入 TIM3 PWM

## Project Structure

```text
AudioSynthesizer/
├─ Core/
│  ├─ Src/
│  │  ├─ main.c
│  │  ├─ freertos.c
│  │  ├─ stm32f1xx_it.c
│  │  └─ usart.c
├─ HARDWARE/
│  ├─ synth.c
│  ├─ synth.h
│  ├─ input.c
│  ├─ input.h
│  ├─ oled_ui.c
│  └─ oled_ui.h
├─ Drivers/
├─ Middlewares/
├─ MDK-ARM/
└─ AudioSynthesizer.ioc
```

## Build & Run

1. 使用 Keil 打开 `MDK-ARM` 工程。
2. 编译并下载到 STM32F103 开发板。
3. 确认外设已连接：OLED、按键、音频输出、MIDI 输入。
4. 上电后可通过：
   - 本地按键触发音符
   - PA0/PA1 切换 Tremolo/Delay
   - 外接 MIDI 键盘输入音符

## Core Parameters (Current)

- `SAMPLE_RATE = 22050`
- `VOICE_COUNT = 4`
- `ATTACK = 8ms`
- `RELEASE = 45ms`
- `TREMOLO_RATE = 5Hz`
- `DELAY = 3308 samples` (~150ms)
- `DELAY_MIX = 35%`
- `DELAY_FB = 40%`

## MIDI Support

- Note On (`0x9n`)
- Note Off (`0x8n`)
- Running Status
- Note On + velocity=0 => Note Off

## Engineering Notes

- 音频关键链路在定时中断中执行，避免任务调度抖动影响听感。
- MIDI 采用 ISR 入队 + 任务解析，降低中断负担。
- 复音增益与软限幅用于抑制和弦峰值失真。
- Delay 支路加入低通与噪声门以抑制反馈噪声。

## Roadmap

- 扩展为完整 ADSR（加入 Decay/Sustain）
- 增加 MIDI CC / Pitch Bend 支持
- Delay 参数可调（time/mix/feedback）
- 参数持久化（Flash 存储）
- 输出升级为 DAC / I2S 方案

## License

本项目采用仓库内 [LICENSE](./LICENSE)。
