# HiPDA for Kindle（hipda-kindle）

越狱 Kindle 上的**原生** HiPDA / Discuz! 7.2 论坛阅读器。由 KUAL 启动的静态 ARM 二进制，直接写 framebuffer；**不是 KOReader 插件、不是网页、不是 Kindle 实验性浏览器里的 H5**。

所有界面（板块列表 / 主题列表 / 帖子正文）都是「一本书」：**整页翻页，没有滚动条**。翻到论坛某一页的最后一屏时，程序自动抓取 `page=2` 并接在后面，阅读不中断。

交互原型：[preview/index.html](preview/index.html)（三套机型、35 张由 C 程序真实渲染的位图，可点屏幕/按 ← → 翻页）。

---

## 1. 先说清楚：论坛侧实测到的事实

2026-11-15 直接请求 `https://www.4d4y.com/forum/` 得到：

| 探测 | 结果 |
|---|---|
| `forumdisplay.php?fid=2` | HTTP 200，但是 `text/html; charset=GBK` 的**提示信息页**：「对不起，您还未登录，无权访问该版块。」页面里自带登录表单 |
| `viewthread.php?tid=1579403` | 同上，游客看不到正文 |
| `logging.php?action=login` | HTTP 200，含 `formhash`、`sid`，表单 `id="loginform"`，`onsubmit="pwmd5('password3')"`，POST 到 `logging.php?action=login&loginsubmit=yes` |
| `index.php` | HTTP 403 + `Just a moment...`（Cloudflare JS 挑战） |
| `archiver/?tid-...html`、`wap/index.php` | 404，无法绕过登录 |
| 页面标识 | `<meta name="generator" content="Discuz! 7.2">`，GBK 编码 |

因此本程序的设计直接由这些事实决定：

- **必须登录**才能看帖；配置里存 `password_md5`（Discuz 的 `pwmd5()` 提交的是 MD5），不存明文。
- **板块清单不抓 `index.php`**，改用 `GreenSkinMonster/hipda` 的 `HiUtils.java FORUMS[]` 内置 fid 表，避开 Cloudflare。
- 页面解析规则来自该客户端的 `HiParserThreadList` / `HiParserThreadDetail` 的 Jsoup selector；`src/discuz.c` 顶部规则表可随论坛改版调整。
- 响应一律经 `src/gbk_table.h`（GBK→Unicode，24066 项）转成 UTF-8，不依赖设备上的 `iconv`/gconv 模块。

> 我没有论坛账号，**登录后真实页面的 DOM 未实测**。`fixtures/synthetic_*.html` 是依据 Discuz! 7.2 默认模板 + 上述 selector 构造的 GBK 合成样本，用于解析器回归测试；`fixtures/gate_real.html`、`cloudflare_real.html`、`login.html` 是真抓的原始响应。真机上请先用 `--dump` 对照一次（见第 9 节）。

---

## 2. 为什么是这套架构

Amazon 的 Kindlet / KDK 在 2013 年就停了，越狱 Kindle 上做原生应用实际只剩「KUAL 启动一个 ARM 二进制」这条路。

```
KUAL menu.json
   └─ scripts/hipda.sh        （关 framework、禁屏保、带 cookie 启动）
        └─ bin/hipda          （静态 C99 二进制）
             ├─ net_curl.c    系统 curl + Netscape cookie jar，execvp 无 shell
             ├─ gbk.c         GBK → UTF-8 内置表
             ├─ html.c        轻量 tag-soup：实体、<br>、表情、引用、附件、script
             ├─ discuz.c      板块 / 主题列表 / 帖子详情 / 登录页解析
             ├─ layout.c      行流 → 固定页数（Book）；跨论坛页自动续接
             ├─ font.c        Unifont 16x16 点阵，ASCII 1 cell / 汉字 2 cells
             ├─ render.c      整页 8bpp Y8 位图（页眉 / 行 / 页脚 / 进度条）
             ├─ ui_fbink.c    FBInk：只负责贴位图 + e-ink 刷新
             └─ input_evdev.c /dev/input/event*：触摸与物理翻页键
```

关键取舍：

- **FBInk 只做最底层的事**（framebuffer 映射、einkfb / mxcfb / MTK 的刷新 ioctl、波形）。它明确支持从 K2 起的全部 Kindle 机型，把硬件差异吃掉。
- **中文不依赖 FreeType / HarfBuzz / 系统字体**。程序自己用 Unifont 点阵把整页画成 Y8 位图，再交给 `fbink_print_raw_data()`。FBInk 只需 `MINIMAL=1 DRAW=1 IMAGE=1`，交叉编译门槛最低，老机型也不会因为缺字体库而白屏。
- **同一套位图在 PC 上导出为 PGM/PNG**，所以 `preview/` 里的画面就是真机会显示的像素，不是另画的示意图。
- **网络走系统 `curl`** 而不是链接 libcurl：各固件自带的 curl 已有可用 CA 与 gzip，省掉为 5 种 ABI 交叉编译 OpenSSL 的麻烦；`-b/-c` 的 cookie jar 正好对应 Discuz 的 `cdb_auth`。

---

## 3. 「整页翻页」是怎么做的

`Book` 是所有界面共用的模型：一个扁平的 `Line[]`，按 `Geometry.rows` 切成固定页。

- `geometry_compute()` 从 `fbink_get_state()` 拿真实 `view_width / view_height / screen_dpi`，`scale = clamp((dpi+50)/100, 1, 4)`，再反算每行 cells 与每页行数。没有任何机型分辨率硬编码。
- 正文按 UTF-8 显示宽度折行（ASCII 1、CJK 2、组合字符 0），并处理中文标点避头尾。
- 翻到最后一页时若 `thread_page < last_page`，抓取下一论坛页并 `book_from_thread(append=1)`；**追加前先把旧的半页补空白到页边界**，否则重新分页会把新楼层填进旧页、被翻页动作跳过。这是最容易踩的一个坑。
- 刷新策略与 Kindle 原生阅读器一致：平时 `WFM_AUTO` 局部刷新，每 `full_refresh_every`（默认 6）页做一次 GC16 全刷消残影。

操作（与 Kindle 阅读器习惯对齐）：

| 区域 / 按键 | 动作 |
|---|---|
| 右侧 35% 屏幕 | 下一页（整页） |
| 左侧 25% 屏幕 | 上一页 |
| 中间某一行 | 进入板块 / 打开帖子 |
| 顶部中间 12% | 菜单（操作说明） |
| 底部中间 12% | 返回上级 |
| 物理翻页键 / 五向键左右 | 上一页 / 下一页 |
| 五向键上下 + 确认 | 选择列表项并打开 |
| 菜单键 / Home / 返回键 | 菜单 / 首页 / 返回 |

---

## 4. 各版本 Kindle 的适配方式

**运行期自适应**（同一份二进制，不做机型分叉）：

| 差异 | 处理方式 |
|---|---|
| 600×800 / 758×1024 / 1072×1448 / 1860×2480… | `fbink_get_state()` 读实际宽高，重算 cols/rows/边距 |
| 167 / 212 / 300 dpi | DPI 决定点阵放大倍数与字号 |
| 8bpp / 16bpp / 32bpp 帧缓冲 | FBInk 负责位深转换（8bpp 有直拷快路径） |
| einkfb（K2/K3）/ mxcfb（K5+）/ MTK（新机型）刷新 ioctl | FBInk 内部按 `device_id` 分派 |
| 有触摸 / 只有物理键（K3、K4、K4NT） | 扫描 `/dev/input/event*`：有 `EV_ABS`+`BTN_TOUCH` 当触屏，纯 `EV_KEY` 当键盘；触摸坐标按 `EVIOCGABS` 的 min/max 归一化 |
| 触摸面板坐标交换/镜像 | 程序按比例区域判定（不是绝对坐标），配合 FBInk 已处理的面板方向 |

预览里实测生成了三套几何：`600x800@167`（K3/K4）、`758x1024@212`（PW3/Voyage）、`1072x1448@300`（PW4/Oasis），同一份 C 代码分别得到 `cols=33/43/40`、`rows=16/22/20`。

**编译期要注意 ABI**（这一点无法自动适配）：社区资料显示 Kindle 在 5.16.3 之后部分机型切到 `armhf`（硬浮点，`/lib/ld-linux-armhf.so.3`），更早是 `armel`（软浮点）。请按设备实际环境选交叉工具链：

```sh
# 在 Kindle 上（USBNetwork / SSH）
uname -m; cat /etc/version; ls -l /lib/ld-linux*; ldd --version | head -1
```

- `armel` → `CROSS=arm-linux-gnueabi-`
- `armhf` → 用对应 hard-float 工具链，例如 `CROSS=arm-linux-gnueabihf-`（**该参数需按你的工具链实际前缀填写，本项目未在任何真机 ABI 上链接验证过**）

---

## 5. 构建

### PC 端（解析/排版/渲染验证，不需要 Kindle）

需要任一 C 编译器（`zig cc` / gcc / clang）和 Pillow：

```sh
python tools/build.py          # 编译 → 54 项自测 → 三机型导出 → 生成 preview/index.html
# 或者
make pc && make test && make preview
```

`tools/build.py` 会自动找编译器，也可用 `CC=...` 指定。**没有编译器时**它能从 Python 的 `ziglang` 包里找 `zig cc`。

### Kindle 端

需要 [koxtoolchain](https://github.com/koreader/koxtoolchain) 生成的交叉编译器和 FBInk：

```sh
git clone https://github.com/NiLuJe/FBInk.git vendor/FBInk
cd vendor/FBInk && make kindle MINIMAL=1 DRAW=1 IMAGE=1 && cd ../..
make kindle CROSS=arm-linux-gnueabi- FBINK_LIB=vendor/FBInk/Release
```

产物 `build/hipda`（静态链接 `-lfbink -lm -lz`）。`include/fbink.h` 已随项目附带，是编译期唯一需要的 FBInk 文件。

已经完成的编译验证：15/15 个源文件用 `zig cc -target arm-linux-gnueabi -DKINDLE_BUILD -Wall -Os` 全部成功生成 ARM 目标文件，零 warning。**但未在真机做最终链接与运行**（本机没有 koxtoolchain / FBInk 的 ARM 静态库，也没有 Kindle 设备）。

---

## 6. 部署到 Kindle

前提：已越狱、已装 KUAL（WinterBreak / LanguageBreak 等按机型选择）、WiFi 可用。把 `kual/hipda/` 整个目录拷到 Kindle 根目录的 `extensions/` 下：

```
/mnt/us/extensions/hipda/
├── config.xml, menu.json, config.ini
├── bin/hipda                  ← build/hipda，需 chmod +x
├── fonts/unifont.bin          ← fonts/unifont.bin（约 2 MB）
├── scripts/hipda.sh, check.sh ← 需 chmod +x
├── cookies.txt                ← 登录后自动产生
└── cache/                     ← 页面缓存，自动产生
```

```sh
# 在 Kindle 上通过 USBNetwork / SSH，或在 Linux 上拷完卡后执行
chmod -R 755 /mnt/us/extensions/hipda
chmod +x /mnt/us/extensions/hipda/bin/hipda /mnt/us/extensions/hipda/scripts/*.sh
```

生成登录配置（在电脑上，密码不进命令行历史、不落明文）：

```sh
python tools/gen_config.py --user 你的用户名 --out kual/hipda/config.ini
# 把生成的 config.ini 覆盖到 /mnt/us/extensions/hipda/config.ini
```

安全弹出 USB → 打开书架里的 KUAL → `HiPDA - 启动论坛`。`HiPDA - 环境自检` 会写 `/mnt/us/extensions/hipda/check.txt`（内核、ABI、curl、/dev/fb0、输入设备、字库、最近日志），白屏时先看它。

---

## 7. config.ini 字段

| 字段 | 说明 |
|---|---|
| `forum_server` | 默认 `https://www.4d4y.com`，可换成自建 Discuz 7.2 站点 |
| `username` / `password_md5` | 登录凭据；`pwmd5()` 用 MD5，留空则游客（只能读缓存） |
| `cookie_file` | Netscape cookie jar；已有 `cdb_auth` 时跳过登录 |
| `font_file` | HUF1 点阵字库路径 |
| `cache_dir` | 原始 HTML 缓存，飞行模式下配合 `--offline` 可读 |
| `font_scale` | 0 = 按 DPI 自动；也可强制 1–4 |
| `full_refresh_every` | 每几页一次全刷，默认 6 |
| `forums` | 首页显示的 fid，逗号分隔；留空显示全部内置板块 |
| `offline` / `no_login` | 只读缓存 / 跳过登录 |

---

## 8. 已知限制（不粉饰）

- **只读**：Kindle 没有中文输入法，本版不实现发帖/回复/搜索/私信。表情显示为文字码，附件图显示为 `〔图片〕` 占位（要显示图片可用 `fbink_print_image`，但会明显增加流量与内存开销）。
- **必须登录**；`index.php` 的 Cloudflare 挑战无法在 Kindle 上完成 JS 验证。若遇到，可在电脑浏览器登录后导出 Netscape 格式 cookie 到 `cookie_file`。
- **登录后的真实 DOM 未实测**：解析规则来自 Android 客户端 selector + Discuz 7.2 默认模板。若论坛改版，用 `--dump viewthread.php?tid=...` 打出解码后的 UTF-8 页面，对照 `src/discuz.c` 的规则表调整。
- **真机未跑过**：本机只做了 PC 全链路自测和 ARM 交叉编译；FBInk 链接、触摸事件、framework 停止/恢复需要在你的设备上验一次。
- 老机型内存小（K3 256MB），启动时脚本会 `stop framework` 释放内存；若仍 OOM，调小 `full_refresh_every` 或减少 `forums`。

---

## 9. 排障与校准

```sh
# PC：打印某个论坛路径解码后的 UTF-8 页面（走 fixtures）
./hipda-pc --dump "viewthread.php?tid=1579403" > page.html

# Kindle：走真实网络抓一页，检查 DOM / 编码 / 登录态
/mnt/us/extensions/hipda/bin/hipda --config ... --dump "viewthread.php?tid=1579403" > /mnt/us/page.html

# 只跑解析器/排版/字体回归
./hipda-pc --selftest
```

---

## 10. 验证证据

- `python tools/build.py` → **54 项自测全部通过**（GBK 转换、UTF-8 宽度、HTML 实体/表情/引用/script、真实 gate 与 Cloudflare 页识别、真实登录页 formhash、主题列表、帖子详情、整页分页边界、点阵字形绘制）。
- **15/15** 个源文件交叉编译为 `arm-linux-gnueabi` 目标文件，零错误零警告。
- 三套机型共 **35 张** 由 C 渲染器输出的真实位图（K3 14 / PW3 10 / PW4 11），见 `preview/`。
- 真实抓包样本保留在 `fixtures/*_real.html` 与 `fixtures/login.html`；合成样本由 `tools/make_fixtures.py` 生成并明确标注。

---

## 11. 目录

```
include/  hipda.h, fbink.h        src/      全部 C 源码（PC 与 Kindle 共用同一套业务层）
fonts/    unifont.bin + 许可说明   fixtures/ 真实响应 + 合成回归样本
kual/     KUAL 扩展包             preview/  交互原型 + 三机型真实位图
tools/    数据生成、构建、预览、配置生成
Makefile  两条构建线（pc / kindle）
```

## 12. 许可

项目源码按 GPLv3-or-later 使用（因为静态链接了 GPLv3 的 FBInk）。`fonts/unifont.bin` 是 GNU Unifont 13.0.04 的生成数据，遵守其 GPLv2+ 与字体嵌入例外，详见 `fonts/LICENSE-UNIFONT.txt`。`include/fbink.h` 来自 NiLuJe/FBInk，仅作编译期头文件使用。接口常量与 selector 参考 GreenSkinMonster/hipda（Android 客户端）。
