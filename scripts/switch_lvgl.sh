#!/bin/bash
# 用法: ./scripts/switch_lvgl.sh v9.3.0

set -e

VERSION=${1:?请指定版本号，例如: v9.3.0}

echo "切换 LVGL 到 $VERSION ..."
git -C lib/lvgl checkout "$VERSION"

echo "应用补丁 ..."
for patch in patches/lvgl-*.patch; do
    [ -f "$patch" ] && git -C lib/lvgl apply "../../$patch" && echo "  已应用: $patch"
done

echo "完成。"
