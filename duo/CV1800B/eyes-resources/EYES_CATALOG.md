# 开源可爱/逼真眼睛资源目录

## 资源 1：Adafruit Uncanny Eyes ⭐ 最经典

- **仓库**：https://github.com/adafruit/uncanny_eyes
- **文档**：https://learn.adafruit.com/animated-electronic-eyes
- **分辨率**：128x128（原版）/ 240x240（Chris.nz 改版）
- **驱动支持**：SSD1351 OLED, ST7735/ST7789 TFT, GC9A01

### 可用眼睛样式

| 风格 | 特点 | 适合场景 |
|------|------|---------|
| **defaultEye** | 标准人类眼睛 | 通用机器人 |
| **catEye** | 猫眼，竖瞳 | 猫咪/神秘风格 |
| **doeEye** | 小鹿眼，温柔大眼 | 可爱风格 |
| **dragonEye** | 龙/爬行动物眼 | 酷炫/幻想 |
| **goatEye** | 山羊眼，横瞳 | 搞怪/特别 |
| **naugaEye** | 紫红色虹膜 | 科幻 |
| **newtEye** | 蝾螈/两栖动物眼 | 自然/可爱 |
| **noScleraEye** | 无眼白，纯虹膜 | 极简/诡异 |
| **terminatorEye** | 终结者红眼 | 机械/冷酷 |
| **owlEye** | 猫头鹰眼 | 智慧/神秘 |

### 技术特点
- 完整的眨眼动画（上下眼睑）
- 眼球随机移动 + 平滑过渡
- 瞳孔随虹膜缩放
- 眼睑闭合时为纯黑（适合 AI 状态：sleep/offline）

### 目录位置
```
eyes-resources/adafruit-uncanny-eyes/convert/*/
  - sclera.png      # 眼白纹理
  - iris.png        # 虹膜纹理
  - lid-upper.png   # 上眼睑
  - lid-lower.png   # 下眼睑
  - pupilMap.png    # 瞳孔映射（部分有）
```

---

## 资源 2：ESP32 Halloween Skull 💀 240px 高清版

- **仓库**：https://github.com/dalori/ESP32-uncanny-eyes-halloween-skull
- **分辨率**：240x240 / 375x375 源数据
- **驱动**：GC9A01（圆形屏）

### 可用眼睛样式

与 Adafruit 相同，但升级到高清 240px：
- `default_large.h` - 高清人类眼
- `catEye.h`, `doeEye.h`, `dragonEye.h`, `goatEye.h`
- `naugaEye.h`, `newtEye.h`, `noScleraEye.h`, `owlEye.h`, `terminatorEye.h`

### 特点
- 直接为 GC9A01 1.28" 圆屏优化
- 包含 `TFT_eSPI` 配置示例
- 有 wiring 接线图参考

---

## 资源 3：OpenEmo 🌟 推荐（效果最逼真）

- **仓库**：https://github.com/iEmoBot/OpenEmo
- **分辨率**：240x240
- **驱动**：GC9A01 + TFT_eSPI
- **平台**：ESP32-S3

### 特点
- **效果最逼真**：高分辨率虹膜纹理 + 自然光影
- 支持表情动画（不只是眨眼）
- 代码结构清晰，使用 TFT_eSPI
- 支持自定义眼睛图片

### 预览
见 `openemo/docs/images/001.jpg` —— 绿色虹膜人类眼，在两块 GC9A01 圆屏上的实拍效果。

### 文件位置
```
eyes-resources/openemo/AnimatedEyes/
  - AnimatedEyes.ino    # 主程序
  - config.h            # 配置
  - user_*.cpp          # 各种表情/动画
```

---

## 资源 4：Simple Eyes 🐦 简约风格

- **仓库**：https://github.com/aprgl/eyes
- **平台**：ESP32
- **分辨率**：240x240

### 可用效果

| 效果 | 描述 |
|------|------|
| **colorful_crow** | 彩色乌鸦眼 |
| **bright_eye** | 明亮大眼 |
| **button_eye** | 纽扣眼（卡通） |
| **black_hole** | 黑洞效果 |
| **universe** | 宇宙星空 |
| **moon** | 月亮 |
| **laser** | 激光眼 |

### 特点
- 简单直接，不是完整眼睛动画
- 适合做装饰/氛围灯效果
- 有些是抽象艺术风格，不是拟真眼睛

---

## 推荐方案

针对你的需求（MilkV Duo + GC9A01 圆形屏，作为 AIStatusHub 状态显示）：

### 方案 A：拟真风格 ⭐ 推荐
**使用 OpenEmo 的眼睛纹理**
- 效果最逼真，有高级感
- 可以直接截取虹膜图片作为静态显示
- 眨眼/表情动画可以复用其逻辑

### 方案 B：可爱/卡通风格
**使用 Adafruit 的 `doeEye` 或 `newtEye`**
- 大眼效果，适合可爱机器人
- 眨眼动画成熟稳定

### 方案 C：机械/冷酷风格
**使用 Adafruit 的 `terminatorEye` 或 `dragonEye`**
- 适合显示 "工作中/处理中" 等状态
- 红色/深色配合状态指示

---

## 下一步

1. 你选定一个/一组眼睛风格
2. 我帮你：
   - 把对应的眼睛数据转换成 240x240 RGB565 帧缓冲格式
   - 写一个 `aistatus-hub` 程序，支持状态切换 + 眨眼动画
   - 或者先从静态图片开始（显示不同状态对应不同眼睛表情）
