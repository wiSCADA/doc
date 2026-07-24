# Cairo Multi-HDMI OSD for RK3588 (headless)

无桌面、多路 HDMI 直出的轻量 OSD 示例：`libdrm` + `Cairo` + `FreeType/Fontconfig`。

实现了与讨论一致的三类画面：

- `digital`：黑底大数字钟 + 中文日期
- `world`：2×5 世界模拟时钟墙
- `weather`：圆角天气卡片风格演示

## 硬件前提（重要）

| 项目 | 说明 |
|------|------|
| RK3588 独立显示通道 | 通常最多约 **4 路**（VP0–VP3） |
| 原生 HDMI | 常见 **2× HDMI TX**，其余口多为 DP/MIPI/桥片 |
| 「至少 4 路 4K@60」 | 需要板级确有 4 个可用 CRTC，且连接器能协商 `3840x2160@60` |
| 8 路 HDMI | 超出单颗 RK3588 典型独立输出能力，需分路/多芯片/桥接方案 |

本程序会：

1. 枚举已连接 DRM connector  
2. 为每个输出选择最接近 `--width/--height/--hz` 的 mode（默认 **3840×2160@60**）  
3. 双缓冲 dumb FB + Cairo 绘制后提交显示  

## 4×4K@60 与 Cairo 的关系

- **显示扫描**可以是 4K@60（KMS mode）。  
- **UI 全屏重绘**若用纯 CPU Cairo 跑满 4 路原生 4K@60，通常不现实。  

本工程默认策略：

```text
--hz 60          → 尽量把 HDMI 设成 4K@60
--fps 30         → UI 逻辑刷新 30Hz（可调）
--scale 0.5      → 先按 1920×1080 画，再放大到 4K framebuffer
```

在 RK3588 量产中建议把 `--scale` 放大步骤换成 **RGA**（质量更好、CPU 更省）。当前仓库用 CPU nearest upscale 作为可移植占位。

### 粗算显存/缓冲（仅 framebuffer）

`3840×2160×4字节×双缓冲×4路 ≈ 265 MB`  
再加上字体与绘制中间层，整体内存仍通常低于 Qt 多屏方案。

## 依赖

```bash
# Debian/Ubuntu / 多数 RK 根文件系统
sudo apt install build-essential cmake pkg-config \
  libdrm-dev libcairo2-dev libfreetype6-dev libfontconfig1-dev fonts-wqy-microhei
```

## 编译

```bash
cd cairo-multi-hdmi
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## 运行（设备上，无桌面）

```bash
# 申请 DRM master：建议在 getty/西向启动，不要同时跑 X11/Wayland
sudo ./build/cairo-multi-hdmi --scene digital --outputs 4 \
  --width 3840 --height 2160 --hz 60 --fps 30 --scale 0.5

sudo ./build/cairo-multi-hdmi --scene world --outputs 4
sudo ./build/cairo-multi-hdmi --scene weather --outputs 4 --scale 0.5
```

切换场景：`digital | world | weather`

## 离线验证（无 /dev/dri 的开发机）

```bash
./build/cairo-multi-hdmi --offline ./offline_frames --offline-frames 2 \
  --outputs 2 --width 1280 --height 720 --scale 1.0 --scene weather
```

会写出 PNG，便于检查布局与中文字体。

## 目录

```text
cairo-multi-hdmi/
  include/           公共头文件
  src/
    drm_output.c     多路 DRM connector / CRTC / dumb FB
    renderer.c       Cairo surface + 可选降分辨率绘制
    font.c           Fontconfig 中文字体选择
    scene_*.c        三套示例场景
    main.c           参数解析与主循环
```

## 上板建议

1. 先用 `modetest -c` / `modetest -s` 确认每路能否 `3840x2160@60`。  
2. 先 `--outputs 1 --scale 1.0` 单路打满，再扩到 4 路。  
3. 4 路同时吃力时：降 `--fps`、保持 `--scale 0.5`，或接入 RGA 缩放。  
4. 字体用子集化 TTF，避免全量 CJK 常驻吃内存。  
5. 产品化后改为 atomic page-flip + vsync 事件，而不是每帧 `SetCrtc`（示例为了少依赖事件循环）。

## 许可

示例代码按仓库主项目许可使用；可直接抽进 BSP 应用。
