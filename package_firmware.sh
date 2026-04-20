#!/bin/bash
# ============================================================
# 一键打包脚本：将固件 + 字体库 + 唤醒词模型合并为单个 bin 文件
# 烧录时只需: esptool.py write_flash 0x0 full_firmware.bin
# ============================================================

set -e

# ---------- 路径配置 ----------
BUILD_DIR=".pio/build/esp32s3box"
BOOTLOADER="${BUILD_DIR}/bootloader.bin"
PARTITIONS="${BUILD_DIR}/partitions.bin"
FIRMWARE="${BUILD_DIR}/firmware.bin"
SPIFFS_BIN="spiffs_fonts.bin"
SRMODEL_BIN="srmodels.bin"
DATA_DIR="data"
OUTPUT="full_firmware.bin"

# PlatformIO 工具路径
ESPTOOL="$HOME/.platformio/penv/bin/python $HOME/.platformio/packages/tool-esptoolpy/esptool.py"
MKSPIFFS="$HOME/.platformio/packages/tool-mkspiffs/mkspiffs_espressif32_arduino"

# ---------- 分区地址（来自 esp_sr_16_large.csv）----------
BOOTLOADER_ADDR=0x0
PARTITIONS_ADDR=0x8000
FIRMWARE_ADDR=0x10000
SPIFFS_ADDR=0x610000
SPIFFS_SIZE=0x200000
MODEL_ADDR=0x810000

# ---------- 步骤 1: 检查编译产物 ----------
echo "=== [1/5] 检查编译产物 ==="
for f in "$BOOTLOADER" "$PARTITIONS" "$FIRMWARE"; do
    if [ ! -f "$f" ]; then
        echo "❌ 缺少文件: $f"
        echo "   请先执行 pio run 编译项目"
        exit 1
    fi
done
echo "✅ 编译产物就绪"

# ---------- 步骤 2: 检查唤醒词模型 ----------
echo "=== [2/5] 检查唤醒词模型 ==="
if [ ! -f "$SRMODEL_BIN" ]; then
    echo "❌ 缺少文件: $SRMODEL_BIN"
    echo "   请将唤醒词模型文件放到项目根目录"
    exit 1
fi
echo "✅ 唤醒词模型就绪 ($(du -h "$SRMODEL_BIN" | cut -f1))"

# ---------- 步骤 3: 打包 SPIFFS 字体 ----------
echo "=== [3/5] 打包 SPIFFS 字体库 ==="
if [ ! -d "$DATA_DIR" ]; then
    echo "❌ 缺少 data/ 目录"
    exit 1
fi

$MKSPIFFS \
    -c "$DATA_DIR" \
    -s $SPIFFS_SIZE \
    -p 256 \
    -b 4096 \
    "$SPIFFS_BIN"
echo "✅ SPIFFS 字体打包完成 ($(du -h "$SPIFFS_BIN" | cut -f1))"

# ---------- 步骤 4: 合并所有分区为单个 bin ----------
echo "=== [4/5] 合并为完整固件 ==="
$ESPTOOL --chip esp32s3 merge_bin \
    --output "$OUTPUT" \
    --flash_mode dio \
    --flash_size 16MB \
    --flash_freq 80m \
    $BOOTLOADER_ADDR "$BOOTLOADER" \
    $PARTITIONS_ADDR "$PARTITIONS" \
    $FIRMWARE_ADDR   "$FIRMWARE" \
    $SPIFFS_ADDR     "$SPIFFS_BIN" \
    $MODEL_ADDR      "$SRMODEL_BIN"

echo "✅ 合并完成: $OUTPUT ($(du -h "$OUTPUT" | cut -f1))"

# ---------- 步骤 5: 输出烧录命令 ----------
echo ""
echo "=== [5/5] 烧录指令 ==="
echo "============================================================"
echo "  一键烧录命令 (macOS/Linux):"
echo ""
echo "  python -m esptool --chip esp32s3 \\"
echo "    --port /dev/cu.usbmodem* \\"
echo "    --baud 921600 \\"
echo "    write_flash 0x0 $OUTPUT"
echo ""
echo "  如果 python -m esptool 不可用，可用 PlatformIO 自带的:"
echo "  \$HOME/.platformio/penv/bin/python \$HOME/.platformio/packages/tool-esptoolpy/esptool.py \\"
echo "    --chip esp32s3 --port /dev/cu.usbmodem* --baud 921600 \\"
echo "    write_flash 0x0 $OUTPUT"
echo ""
echo "  Windows 用户请将 --port 改为对应 COM 端口 (如 COM3)"
echo "============================================================"
