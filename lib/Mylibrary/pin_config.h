#pragma once
/**
 * ESP32-S3-LCD-EV-Board-2 (800x480) 引脚配置
 * 主板: ESP32-S3-LCD-EV-Board-MB
 * 音频: ES8311 (codec) + ES7210 (ADC) + NS4150 (PA)
 */

// I2S 音频引脚
#define I2S_MCK_IO    5   // MCLK
#define I2S_BCK_IO   16   // SCLK (BCLK)
#define I2S_WS_IO     7   // LRCK (WS)
#define I2S_DO_IO     6   // Data Out → ES8311 codec DSDIN
#define I2S_DI_IO    15   // Data In  ← ES7210 ADC SDOUT

// 兼容源项目的宏名
#define MCLKPIN   I2S_MCK_IO
#define BCLKPIN   I2S_BCK_IO
#define WSPIN     I2S_WS_IO
#define DOPIN     I2S_DO_IO
#define DIPIN     I2S_DI_IO

// I2C (与触摸共用总线)
#define IIC_SDA   8
#define IIC_SCL   18

// 功放使能 — 通过 IO 扩展器 TCA9554 的 P0 控制
#define PA_PIN    (-1)
