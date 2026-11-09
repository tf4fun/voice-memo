<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐、电池参数等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

### 语音日记字体

应用嵌入 `fonts/memo_font_16.c`（16 px 粗体）和 `fonts/memo_font_20.c`（20 px 常规），
来自 [Noto Sans SC 2.004](https://github.com/notofonts/noto-cjk/tree/Sans2.004)。
版权为 2014–2021 Adobe。字体软件使用 SIL OFL 1.1；分发字体资产时保留
[OFL.txt](fonts/OFL.txt) 和 [NOTICE.txt](fonts/NOTICE.txt)，该许可与应用代码许可独立。

[manifest.json](fonts/manifest.json) 固定官方来源 URL/SHA-256、`lv_font_conv` 1.5.2、
输出指标和生成 C 有效载荷哈希。[charset.txt](fonts/charset.txt) 保存原始请求的 4013 个码点：
GB2312 一级汉字 3755 个、ASCII 及标点/全角形式。两套 cmap 实际各含 4012 个码点；
未分配的 U+FF00 没有源字形，不属于支持的文本集。生成行高为 31/38 px，并非名义字号，
UI 布局测试使用这些实际指标。

具备 Node/npm 与 Python 后，从仓库根目录执行：

```bash
npm install --prefix bin/lvfont --no-audit --no-fund lv_font_conv@1.5.2
python3 tools/generate_memo_fonts.py --fetch --check
```

源字体缓存在忽略跟踪的 `bin/fonts` 下，下载后校验哈希；已有不匹配文件会保留。
`--check` 只写临时文件，逐字节重现位图/指标/cmap 有效载荷，不比较含本地路径的开头 CLI 注释。
`--write` 才明确替换生成 C 文件。两者均使用 4 bpp、无压缩 LVGL 输出；通过
`main/CMakeLists.txt` 编译维护的位图资产，设备不把 OTF 加载到 RAM。

所有静态 UI 文字同时检查两套 cmap。动态文字使用对应字号及 Montserrat 回退；不支持的 Unicode
或非法字节序列显示 `?`，保留支持的文字和 UTF-8 边界。服务器原文及字节偏移不变。
宿主覆盖策略不代表真实设备渲染或峰值 RAM 已验证。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。

## 电池参数（battery）

[`battery/wanli-500mah-profile2-xil40x-20261020.txt`](battery/wanli-500mah-profile2-xil40x-20261020.txt)
保存万锂新能源 500 mAh 电芯的 CW2017 参数（profile2、XIL40X、2026-10-20）。
该文件是项目提供的 `喜梨_智能工卡_万锂新能源500mAh_profile2_XIL40X_20261020.txt`
的原样副本，按维护者要求保留用于本次固件更新；原文件没有附带单独的许可说明。

格式为 80 行两位十六进制数字，每行一个字节，按寄存器顺序排列。
BSP 在 `components/bsp/src/bsp_battery.c` 中嵌入这 80 字节，主机检查会验证
源文件及数组内容完全一致。文本文件 SHA-256：
`7ea96eba4634f4495a37c25ffacd54c1deaa0693b162c892d671a2ba68460919`。
解码后 80 字节 SHA-256：
`fa7365671093ef449c5bdcfb4adb279e3d275f888109f50b6b2f21e9b96220d5`。
这是电量计参数，不是充电配置，也不能通用于所有 500 mAh 电芯；详见
[硬件指南](../docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md#9-cw2017-电池计)。


## 终端 ASCII 字体

`fonts/memo_font_terminal.c` 提供真实的 8 px 固定字宽和 18 px 行高。它仅将 LVGL Unscii-8 的
位图行纵向重复，保持横向像素与字距不变。内置 Unscii-16 的字宽实际是 16 px，不能作为
8 px 列宽字体。覆盖 ASCII U+0020..U+007F，中文继续使用原有 Noto 子集。

固定输入 `fonts/source/lvgl_unscii_8.c` 为项目 LVGL 组件的未修改副本，SHA-256：
`bd95b245eaea139aa7c4d9efbc130cc8eda4c11dc7a22b42048a7f0d814e29bf`。
LVGL 源码及派生字体保留 [`fonts/LICENSE.lvgl.txt`](fonts/LICENSE.lvgl.txt) 中的 MIT 声明，
与 Noto 的 OFL 分开。原路径为 LVGL `src/font/lv_font_unscii_8.c`，组件版本固定在
`dependencies.lock`。离线重建：`python3 tools/generate_memo_terminal.py`，加 `--check` 验证。
CMake 只编译派生字体；宽度检查读取实际生成的 advance，主机对象测试不代表实机光栅效果。
