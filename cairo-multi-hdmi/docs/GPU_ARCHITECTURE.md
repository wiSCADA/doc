# GPU-first multi-output architecture (RK3588, 4–8 HDMI)

## Goal

Fully utilize **Mali-G610** for UI drawing, while driving **4 (or up to 8) HDMI** outputs without burning CPU on Cairo software rasterization.

## Hard limits (read first)

| Item | Reality |
|------|---------|
| RK3588 display controllers | Typically **≤4 independent** video ports (VP0–VP3) |
| Mali-G610 | **One** GPU; bandwidth/fillrate shared |
| “8×HDMI” on one SoC | Often **clone / bridge / MST / dual-SoC**, not 8 unique full 4K@60 GPU frames |
| Wrong approach | Draw the full UI **8 times** per frame on GPU “to use it more” |
| Right approach | Draw **once (or few shared layers)**, then **broadcast / compose** to outputs |

Maximizing GPU usage ≠ maximizing draw-call count. It means moving raster work off CPU onto Mali, and keeping GPU fillrate focused on **unique pixels**.

## Recommended pipeline

```text
                    ┌─────────────────────────────┐
                    │  Scene / UI (logical res)   │
                    │  e.g. 1920×1080 or 4K once  │
                    └─────────────┬───────────────┘
                                  │
                         Mali GLES / Vulkan
                         (render to GBM/DMA-BUF)
                                  │
                    ┌─────────────▼───────────────┐
                    │ Shared color buffer (DMABUF) │
                    └─────────────┬───────────────┘
                                  │
              ┌───────────────────┼───────────────────┐
              ▼                   ▼                   ▼
         RGA scale/blit      RGA scale/blit      VOP plane clone
         → HDMI0 FB          → HDMI1 FB          → HDMI2/3 …
              │                   │                   │
              └───────────────────┴───────────────────┘
                                  │
                         DRM/KMS page-flip
                         4K@60 scanout per CRTC
```

### Roles

| Block | Does | Does not |
|-------|------|----------|
| **GLES/Vulkan** | Paths, text atlases, icons, effects | Own 8 unrelated full-screen UIs every frame |
| **RGA** | 1080p→4K, multi-output copy, format convert | Draw FreeType glyphs |
| **VOP/KMS** | Timing, planes, HDMI 4K60 | Business widgets |
| **CPU** | App logic, layout, font shaping (HarfBuzz), upload | Full-screen software paint |

## Output topology modes

Pick one; product config should declare it.

### Mode A — Same content (best GPU efficiency)

1 GPU frame → N outputs (RGA blit or hardware split).

- GPU load ≈ **1×** scene  
- Ideal for video wall / identical OSD  
- 8 HDMI often appears here via splitters

### Mode B — Shared template + per-output overlay (recommended for clocks/weather)

1. GPU draws **shared background/card chrome** once  
2. Small **per-output** layer: city name, timezone, HDMI index, local time  
3. RGA/VOP compose shared + overlay per connector  

- GPU load ≈ **1× heavy + N× tiny**  
- Fits digital clock / weather with different cities per screen  

### Mode C — Fully independent UIs

N full scenes / frame.

- GPU load ≈ **N×**  
- 4×4K@60 independent is already very heavy on G610  
- 8× unique 4K@60 on one RK3588 is generally **not** a sound design  

If product needs 8 unique 4K programs, plan **multi-SoC** or lower resolution / lower FPS / tiled regions.

## What to render at

| Strategy | When |
|----------|------|
| Logical **1080p**, RGA upscale to 4K | Default for OSD; best FPS/power |
| Native **4K** once, clone to N | When UI has fine 4K text and GPU headroom |
| Dirty regions / two layers | Clocks (static face + moving hands/text) |

Display mode can still be **3840×2160@60** even if UI redraw is 30 FPS or dirty-only.

## API choice on RK3588

| API | Role |
|-----|------|
| **OpenGL ES 3.x + GBM/EGL** | Practical default for headless GPU UI |
| **Vulkan** | Longer-term; better explicit sync/multi-queue |
| **Cairo** | Dev fallback / offline; not the 4–8 HDMI production path |
| **libmali vs Panthor/Mesa** | Board BSP choice; verify GBM + 4K FB allocation on *your* image |

## Threading model

```text
main / logic thread
  → build scene commands (CPU)

1 render thread (EGL context current)
  → GLES draw shared (+ overlays)

present threads or single present loop
  → RGA jobs per output (non-overlapping dst)
  → DRM flips (one per CRTC, vsync paced)
```

Do **not** create 8 EGL contexts that each redraw the world. One context, one shared target, N presents.

## Memory ballpark (4×4K)

- 4×4K XRGB double-buffer scanout ≈ **~265 MB**  
- Plus 1 shared GPU render target (1080p or 4K)  
- Glyph atlas + textures  

Still usually far below Qt multi-screen, if you avoid N full unique 4K GPU backbuffers for intermediate UI.

## Migration from current Cairo demo

1. Keep DRM output enumeration / modeset (already in-tree)  
2. Replace Cairo framebuffer paint with **GLES → DMA-BUF**  
3. Add **RGA present** path for scale/multi-copy  
4. Keep Cairo `--backend cairo` for desktop CI / offline PNG  
5. Scene code should emit **primitives/widgets**, not call Cairo directly (gradually)

This repo’s `backend` switch is the first step of that migration.
