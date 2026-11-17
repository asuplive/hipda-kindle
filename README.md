# HiPDA for Kindle（hipda-kindle）

越狱 Kindle 上的**原生** HiPDA / Discuz! 7.2 论坛阅读器。由 KUAL 启动的静态 ARM 二进制，直接写 framebuffer；**不是 KOReader 插件、不是网页、不是 Kindle 实验性浏览器里的 H5**。

所有界面（板块列表 / 主题列表 / 帖子正文）都是「一本书」：**整页翻页，没有滚动条**。翻到论坛某一页的最后一屏时，程序自动抓取 `page=2` 并接在后面，阅读不中断。

**可直接部署的产物**：`build/` 目录下**序号最大**的 `hipda-kindle-armv6_<N>.zip`（解压到 Kindle 根目录即可）。本机沙箱只允许每个路径首次写入，所以构建/打包产物一律带自增序号、不覆盖旧文件——**序号越大越新**，`build/hipda_<N>` 同理。包内含 ARMv6 纯静态可执行文件、HUF1 字库、已填凭据的 config.ini、KUAL 菜单（含「屏幕自检」入口）与完整 README。一键重建：`python tools/build_kindle.py && python tools/check_elf.py && python tools/package_kindle.py`。

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

操作（**点击翻页**，滑动不再翻页）：

| 区域 / 按键 | 动作 |
|---|---|
| **点屏幕右侧（约 38%）** | 下一页（整页） |
| **点屏幕左侧（约 30%）** | 上一页 |
| **左上角「Hi」按钮** | 打开主菜单（10 项，见下） |
| **右上角圆圈刷新键** | 重新拉取当前页（**唯一**的联网刷新入口） |
| **页脚「‹ 上一页 / 下一页 ›」** | 切换论坛页码（帖子列表 / 帖子正文的第 1、2、3… 页） |
| **页脚「返回」** | 返回上一级 |
| 点某一行 | 进入板块 / 打开帖子（条目优先于翻页） |
| 底部 12% | 返回上级 |
| 物理翻页键 / 五向键左右 | 上一页 / 下一页 |
| 五向键上下 + 确认 | 选择列表项并打开 |
| 菜单键 / Home / 返回键 | 菜单 / 首页 / 返回 |

> **为什么取消滑动翻页**：e-ink 刷新一次要几百毫秒，滑动过程会连续产生触摸事件，手柄一样地触发整页重绘，结果就是"越划越卡"。现在滑动被直接忽略（不产生任何动作、不重绘），翻页只靠点击或物理键。判定为拖动后只丢弃事件，不再画一帧。
>
> 三处性能修复：① 主循环以前在"本轮没有识别出动作"时也会重绘整页，等于每次空轮询都刷一次屏；现在只有真正的事件才重绘。② 按下反馈与列表选择移动都改为**只刷新受影响的那几行**（约几十 KB），不再整页刷新。③ 翻页默认用 **WFM_DU 快波形**（约 260ms，纯黑白、无闪），每 `full_refresh_every` 页补一次 GC16 全刷清残影；设置里可切回"清晰"档（GC16，约 450ms）。
>
> **加载不卡界面，且可取消**：抓网页在 `fork()` 出来的子进程里执行，父进程每 150ms 轮询一次子进程状态，期间继续读触摸——加载时点屏幕不会被吞掉。加载页**点任意位置即取消**，会把子进程连同它派生的 curl 一起杀掉（子进程自成一个进程组），界面回到原来的位置并提示「已取消」。只是按住不放（只有按下、未抬起）不算取消，避免误触。
>
> **缓存与懒加载**：进入板块 / 打开帖子时先查**内存快照**，命中就零延迟直接显示（Book 结构整体交换，不做拷贝）；未命中才读磁盘缓存（`cache_dir` 下的 HTML），磁盘也没有才联网。所以「返回」是瞬时的，重新打开读过的帖子也是瞬时的。**只有点右上角刷新键才会重新联网**。翻到书页末尾时自动续抓论坛下一页（`append`），这是唯一的按需加载路径，不会一次性把所有楼层拉下来。设置 →「清理缓存」可删除所有缓存的 HTML（保留 Cookie，不会退出登录）。

**主菜单（点左上角 Hi）**：

| # | 项目 | 说明 |
|---|---|---|
| 1 | 论坛 | 返回板块首页 |
| 2 | 搜索帖子 | 屏幕键盘输入关键字，走 `search.php` |
| 3 | 我的帖子 | `my.php?item=threads` |
| 4 | 我的回复 | `my.php?item=posts` |
| 5 | 我的收藏 | `my.php?item=favorites` |
| 6 | **收藏离线缓存** | 抓取收藏列表全部分页，逐个把收藏帖子正文下载到本机；之后离线也能看 |
| 7 | 短消息 | `pm.php` |
| 8 | 帖子消息 | `notice.php` |
| 9 | 设置 | 字号 / 主题 / 全刷频率 / 登录 |
| 10 | 切换主题 | 白底黑字 ↔ 黑底白字，立即生效并写回 config.ini |
| 11 | **退出应用** | 明确退出，触摸交还系统 |

**登录**：**安装包不自带任何账号**（`config.ini` 里 `username=` / `password_md5=` 均为空），所以首次启动会直接进登录页。屏幕键盘有**小写 / 大写 / 数字符号三页**，控制行 `[Sh]` 切大小写、`[Pg]` 切数字符号、`[Sp]` 空格、`[Bk]` 退格、`[Ok]` 确定、`[Xx]` 取消；字符键按 3 格间距排布，不易误触。输入框是带边框的样式，当前输入框末尾有下划线光标（`[_]`），类似常见文本输入框。大屏机型上表单与键盘之间还有额外留白（`rows < 20` 的老机型如 K4/K5 会自动用紧凑版，保证一页放得下）。密码只在内存里算成 MD5 后写入 `config.ini`，不保存明文。登录成功后自动回到论坛首页。已登录时可在「设置 → 登录 / 切换账号」换号。

> 3–7 这几项抓的是 Discuz 的标准页面，再用 HTML 转文本整页翻页显示。不同版本的模板差异可能让排版不完美，但内容可读；若某项返回空白，用 `--dump` 看原始页面再调 `src/discuz.c`。

---

## 4. 各版本 Kindle 的适配方式

**运行期自适应**（同一份二进制，不做机型分叉）：

| 差异 | 处理方式 |
|---|---|
| 600×800 / 758×1024 / 1072×1448 / 1264×1680 / 1860×2480… | `fbink_get_state()` 读实际宽高，重算 cols/rows/边距 |
| 167 / 212 / 300 dpi | DPI 决定点阵放大倍数与字号 |
| 8bpp / 16bpp / 32bpp 帧缓冲 | FBInk 负责位深转换（8bpp 有直拷快路径） |
| einkfb（K2/K3）/ mxcfb（K5+）/ MTK（新机型）刷新 ioctl | FBInk 内部按 `device_id` 分派 |
| 有触摸 / 只有物理键（K3、K4、K4NT） | 扫描 `/dev/input/event*`：有 `EV_ABS`+`BTN_TOUCH` 当触屏，纯 `EV_KEY` 当键盘；触摸坐标按 `EVIOCGABS` 的 min/max 归一化 |
| 触摸面板坐标范围异常 | 优先读 `ABS_MT_POSITION_X/Y` 的 `EVIOCGABS`；若范围明显小于屏幕（cyttsp5 的 `ABS_X` 只报 0..1），退回「按屏幕像素 1:1」映射 |
| 触摸面板坐标交换/镜像 | 程序按比例区域判定（不是绝对坐标），配合 FBInk 已处理的面板方向 |

预览里实测生成了三套几何：`600x800@167`（K3/K4）、`758x1024@212`（PW3/Voyage）、`1072x1448@300`（PW4），同一份 C 代码分别得到 `cols=33/43/40`、`rows=16/22/20`。**真机 KO3（Oasis 3）实测为 1264×1680 @300**，自动算出 `cols=48 rows=24`，无需改代码。

**编译产物已按最老机型构建**：`build/hipda` 是 `arm-linux-musleabi`、`-mcpu=arm1136jf_s`（**ARMv6 软浮点**）的**完全静态** ELF，ELF 里没有 `PT_INTERP`、没有 `PT_DYNAMIC`、0 个未定义符号（由 `tools/check_elf.py` 验证）。因此：

- ARMv6 指令在 K2/K3/K4/K5（ARM11）和之后的 ARMv7 机型（KT/PW/KV/KOA/Scribe）上都能跑，不会出现 `Illegal instruction`；zig 默认的 `armv7a` 会让老机型直接崩，所以这里显式降级。
- 不依赖设备上的 glibc，也不依赖 `ld-linux-armhf.so.3`——社区资料里 5.16.3 之后的 `armel`/`armhf` 分叉**对这个二进制不适用**。只有当你改用 koxtoolchain + 动态链接重编时才需要选 ABI。
- 想为纯新机型优化可设 `HIPDA_ARM_CPU=cortex_a9` 重编（会失去 ARMv6 兼容性）。

**待人工核实**：K2/K3 系列是很老的 2.6.x 内核，musl 静态二进制在那类内核上的 syscall 兼容性没有真机实测。若启动即 `FATAL` 或直接退出，请改用 koxtoolchain 的 glibc 静态构建（见第 5 节备选路径）。

---

## 5. 构建

### PC 端（解析/排版/渲染验证，不需要 Kindle）

需要任一 C 编译器（`zig cc` / gcc / clang）和 Pillow：

```sh
python tools/build.py          # 编译 → 66 项自测 → 三机型导出 → 生成 preview/index.html
node tools/check_preview.js    # 预览交互冒烟
# 或者
make pc && make test && make preview
```

`tools/build.py` 会自动找编译器，也可用 `CC=...` 指定。**没有编译器时**它能从 Python 的 `ziglang` 包里找 `zig cc`。

### Kindle 端（一键，已在本机跑通）

不需要 koxtoolchain、不需要 make：用 `zig cc` 当 ARM 交叉编译器，直接把 FBInk 编成静态库再静态链接。

```sh
python tools/fetch_fbink.py    # 从 jsdelivr 拉取 FBInk 最小构建所需 39 个文件（已随项目附带）
python tools/build_kindle.py   # libfbink.a + 15 个应用 .o -> build/hipda
python tools/check_elf.py      # 校验 ELF / ARMv6 / 静态性 / 内容指纹
python tools/package_kindle.py # 生成 build/hipda-kindle-armv6.zip
```

或者 `make kindle-easy` 一次跑完这四步。产物：

| 文件 | 说明 |
|---|---|
| `build/libfbink.a` | FBInk `MINIMAL=1 DRAW=1 IMAGE=1` 的 **ARM 静态库**（4 个目标文件，263 KB） |
| `build/hipda` | **ARMv6 软浮点纯静态可执行文件**（220 KB，已 strip） |
| `build/manifest.json` | 目标、CPU、宏、SHA-256 |
| `build/hipda-kindle-armv6.zip` | 可直接解压到 Kindle 的 KUAL 扩展包 |
| `build/SHA256SUMS.txt` | 包、二进制、静态库、字库的校验和 |

FBInk 的编译宏与 `ui_fbink.c` 使用同一组：`-DFBINK_FOR_KINDLE -DFBINK_MINIMAL -DFBINK_WITH_DRAW -DFBINK_WITH_IMAGE`，避免头文件里的条件结构体造成 ABI 不一致。

**备选**：若你已有 [koxtoolchain](https://github.com/koreader/koxtoolchain)，仍可用 `make kindle CROSS=... FBINK_LIB=...`；此时需要先用 `make -C vendor/FBInk kindle MINIMAL=1 DRAW=1 IMAGE=1` 生成对应 ABI 的 `libfbink.a`。

**本机限制**：zig 0.17 在中文用户目录下写缓存会报 `manifest_create AccessDenied`，两条构建脚本都会自动挑一个纯 ASCII 的可写数据盘作 `ZIG_LOCAL_CACHE_DIR` / `ZIG_GLOBAL_CACHE_DIR`。若仍遇到 `AccessDenied`、`failed to rename` 或 `lld-link: Stream Error`（本机后期出现过，属于文件系统 I/O/缓存锁问题，不是源码错误），可以：

```sh
# 用新建的纯 ASCII 目录
ZIG_LOCAL_CACHE_DIR=J:/zig1 ZIG_GLOBAL_CACHE_DIR=J:/zig2 python tools/build_kindle.py
# 或者关掉自动修复，用 zig 默认缓存
HIPDA_NO_ZIG_CACHE_FIX=1 python tools/build.py
```

**随包已附带构建好的 `build/hipda`、`build/libfbink.a` 和部署 zip，直接部署不需要重新编译。**

---

## 6. 部署到 Kindle

前提：已越狱、已装 KUAL（WinterBreak / LanguageBreak 等按机型选择）、WiFi 可用。

**最简单**：把 `build/hipda-kindle-armv6.zip` 解压到 Kindle USB 盘根目录，会自动得到 `extensions/hipda/...`（zip 内已给 `bin/hipda` 和 `scripts/*.sh` 设好 755 权限）。也可以手工把 `kual/hipda/` 拷过去：

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

### 白屏排查（KO3 / 任意机型）

**KO3 白屏的真正根因（已确认并修复）**：旧 KUAL 脚本里的 `/etc/init.d/framework stop` 会连带杀掉脚本自己——KUAL 的 action 是 `lab126_gui` 的子进程，停 framework 等于把父进程树一起干掉。结果是脚本停在 `framework stop` 那一行，后面的 `echo >> hipda.log` 和 `./bin/hipda` 全没执行：屏幕因 framework 已停而变白，但程序从未启动。判据非常明确：`check.txt` 里 `hipda.log: No such file or directory`，而二进制/字库/配置都是新的。**修复：脚本彻底删掉 `framework stop/start`**。FBInk 直接写 framebuffer，本来就不依赖停掉 UI；改用 `lipc-set-prop preventScreenSaver` 防止屏保即可。脚本现在第一行就写日志，无论后面发生什么都能看到执行到哪。

另一个已修复的隐患：FBInk 要求 Kindle 上的 Y8 位图必须设 `ignore_alpha=true`，旧版没设，FBInk 会把它当 YA 两通道重新交织，在部分 mxcfb / 反相灰阶面板上会输出全白。同时字库加载失败时不再画纯白屏，而是画黑色条纹提示，并按多个候选路径重试字库。

不 stop framework 的一个副作用：Amazon UI 可能在锁屏/唤醒时重绘。程序内部已每 6 页全刷一次消残影，且 KUAL action 运行期间 UI 一般不会主动覆盖 framebuffer；若真机上发现被覆盖，可改用 `lipc-set-prop com.lab126.winmgr` 的暂停机制，而**不要**回到 `framework stop`。

如果换新包后**仍然白屏**，用菜单里的 `HiPDA - 屏幕自检(白屏排查)` 区分根因。它不读字库、不联网，只画三个高对比黑色条带 + 边框：

- **能看到黑色条带** → 屏幕/刷新通路正常，问题在字库或解析；把 `extensions/hipda/diag.txt` 拷出来看日志。
- **自检还是白屏** → 与程序逻辑无关，是 FBInk/framebuffer 或 e-ink 波形问题。把 `diag.txt` + `hipda.log` + `check.txt` 一起拷出，重点看 `display:` 与 `ui_present(...)` 两行的返回值。

也可以手动跑（**不要**加 `framework stop`，会杀掉你自己的 shell）：

```sh
cd /mnt/us/extensions/hipda
./bin/hipda --diag ; cat diag.txt
```

### 触摸与退出（真机行为）

不停 framework 后，触摸事件默认会同时送到 Amazon UI 和本程序，表现为「翻页的同时弹出系统状态栏/搜索框」。程序在 `input_open()` 里对触摸面板调用 `EVIOCGRAB` **独占**输入，系统就收不到触摸了（与 KOReader 同款做法）。日志里会有 `input: grabbed /dev/input/eventN (touch exclusive)`；若显示 `grab failed`，说明权限不足，把 check.txt 发回来。程序退出或崩溃时内核会随 fd 关闭自动释放独占，不会把机器锁死。

**退出方法**：在论坛首页点屏幕**底部** → 出现「退出 HiPDA？」确认页 → 再点一次底部即退出；点其他位置取消。退出后画面会停在「已退出」提示上，点一下屏幕 Amazon UI 就会重绘并恢复（这是正常的，因为系统没收到事件不会主动重绘）。

### 触摸校准（点哪都不对 / 方向反了）

有的触摸面板（或它的安装方向）上报的坐标与帧缓冲是**互换或镜像**的，表现为"点左边响应右边""点上面响应下面"。程序内置校准向导：

**设置 → 触摸校准**，屏幕四角会依次亮起一个方块，按 **左上 → 右上 → 右下 → 左下** 的顺序点完四个角，程序会自动算出 `cal_swap`（X/Y 是否互换）与 `cal_flip_x` / `cal_flip_y`（是否左右/上下镜像），写入 `config.ini` 并立即生效。之后不用再校准。

日志里能看到每一步：`calib: corner N raw=(x,y)` 和最终的 `calib: saved swap=… flip_x=… flip_y=…`。若四个角的 raw 坐标与你的点击位置明显不符（例如点左上却得到很大的 x），说明轴范围本身有问题，把日志发回来。

**按压反馈**：手指按下时，被按住的那一行立刻**反色（黑底白字）**，抬起后执行动作——这样在慢速 e-ink 上也能确认"点到了"。

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
| `theme` | 0 = 白底黑字（默认），1 = 黑底白字。菜单「切换主题」会改这项并立即写回 |
| `offline` / `no_login` | 只读缓存 / 跳过登录 |

---

## 8. 已知限制（不粉饰）

- **只读**：Kindle 没有中文输入法，本版不实现发帖/回复/搜索/私信。表情显示为文字码，附件图显示为 `〔图片〕` 占位（要显示图片可用 `fbink_print_image`，但会明显增加流量与内存开销）。
- **必须登录**；`index.php` 的 Cloudflare 挑战无法在 Kindle 上完成 JS 验证。若遇到，可在电脑浏览器登录后导出 Netscape 格式 cookie 到 `cookie_file`。
- **登录后的真实 DOM 未实测**：解析规则来自 Android 客户端 selector + Discuz 7.2 默认模板。若论坛改版，用 `--dump viewthread.php?tid=...` 打出解码后的 UTF-8 页面，对照 `src/discuz.c` 的规则表调整。
- **真机状态（2026-11-16，Kindle Oasis 3 / KO3，内核 4.1.15-lab126 armv7l）**：已在真机跑通启动、联网、解析与整页渲染，画面正常。过程中确认并修复了三个真机专属问题：① KUAL 脚本里的 `framework stop` 会自杀导致白屏无日志（已移除）；② 触摸事件被系统同收导致点一下弹状态栏/搜索框（已用 `EVIOCGRAB` 独占）；③ 原先没有退出入口（已加首页底部两次点击退出）。**仍未验证**：物理翻页键、极老 K2/K3 的 2.6.x 内核兼容性、长时间阅读的残影表现。
- **ASan 在本机不可用**：zig 0.17 的 Windows ASan 运行库缺 `__asan_handle_no_return`，链接失败；已改用 UBSan + 畸形输入压测替代（见第 10 节），但没有做地址消毒器级的堆越界检测。
- 老机型内存小（K3 256MB）；脚本**不会**再停 framework（那会杀掉 KUAL action 自己）。若出现 OOM，调小 `full_refresh_every` 或减少 `forums`。
- 极老内核（K2/K3 的 2.6.x）对 musl 静态二进制的兼容性**未实测**，见第 4 节。

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

- `python tools/build.py` → **66 项自测 0 失败**（GBK 转换、UTF-8 宽度、HTML 实体/表情/引用/script、真实 gate 与 Cloudflare 页识别、真实登录页 formhash、主题列表、帖子详情、整页分页边界、点阵字形绘制 + 20 个畸形用例 + 256 组随机输入 + 真实登录页面断言）。
- **UBSan 版同样 66/66 通过，日志里 0 条 `runtime error`**：未发现未定义行为。畸形输入压测覆盖空串、截断标签、非法 UTF-8/GBK 尾字节、未闭合属性、超长实体、256 组随机字节，喂给 `html_to_text` / `gbk` / `discuz_parse_*` / `book_wrap_text` 全链路。
- **ARM 静态库与可执行文件**：`libfbink.a`（4 个 ARM 目标文件）+ 15 个应用 `.o` 静态链接成 `build/hipda_5`（最终版）；`tools/check_elf.py` 校验结果：`ELF32 / little-endian / ARM / EXEC`、`PT_INTERP=none`、`PT_DYNAMIC=none`、0 个未定义符号、`Tag_CPU_arch=8 (ARMv6)`、5/5 内容指纹（`/dev/fb0`、`mxcfb`、`HUF1`、`forumdisplay.php`、`HiPDA`）。
- 三套机型共 **35 张** 由 C 渲染器输出的真实位图（K3 14 / PW3 10 / PW4 11），`node tools/check_preview.js` 无头交互冒烟 PASS。
- 修复的测试健壮性问题：自测原本写固定的 `.cache/selftest.pgm`，Windows 上该文件被占用时会误报失败；改为每次写唯一文件名并额外断言文件确实落盘。
- **真实账号登录实测（2026-11-15，账号由用户提供）**：完全按程序的请求序列（fresh GET formhash → POST MD5 密码 → inajax=1）登录成功，服务器返回「欢迎您回来」并下发 6 个 Set-Cookie（含 `cdb_auth`）；用该会话抓取真实页面（Discovery 版块 75 个主题、1579403 帖 50 层 / 105 页）存为 `fixtures/live_*.html`。
- **实测揪出并修复 3 个真 bug**：
  1. `net_has_auth_cookie()` 把 curl 写的 `#HttpOnly_…cdb_auth` 注释行当普通注释跳过 → 登录成功也判成未登录（真机必踩）；
  2. 登录成功判定含 `discuz_login_ok()`，但实测发现「欢迎您回来」的响应可能**不带任何 Set-Cookie**（过期会话），会话根本不保持 → 改为只认 `cdb_auth` cookie；
  3. 真实主题行的 folder 图标链接（无文本）排在标题链接前，`extract_a_text()` 取第一个匹配导致**真实标题解析为空** → 改为扫描所有匹配取第一个非空；`discuz_last_page()` 全文扫描被正文引用链接污染 → 限定在 `class="pages"` 块内解析。
- **live 回归**：66 项自测含真实登录页面断言（≥50 主题、≥40 楼层、标题/作者/分页），`HIPDA_LIVE=1` 导出 48 屏真实渲染位图（`preview/live/`）。
- 真实抓包样本保留在 `fixtures/*_real.html`、`fixtures/login.html` 与 `fixtures/live_*.html`；合成样本由 `tools/make_fixtures.py` 生成并明确标注。

---

## 11. 目录

```
include/  hipda.h, fbink.h        src/      全部 C 源码（PC 与 Kindle 共用同一套业务层）
fonts/    unifont.bin + 许可说明   fixtures/ 真实响应 + 合成回归样本
kual/     KUAL 扩展包（含 ARM 二进制） preview/  交互原型 + 三机型真实位图
vendor/   FBInk 最小构建源码       build/    libfbink.a、hipda、zip、SHA256SUMS
tools/    fetch_fbink / build_kindle / check_elf / package_kindle / build / make_fixtures / gen_*
Makefile  pc、kindle、kindle-easy、preview 等目标
```

## 12. 许可

项目源码按 GPLv3-or-later 使用（因为静态链接了 GPLv3 的 FBInk）。`fonts/unifont.bin` 是 GNU Unifont 13.0.04 的生成数据，遵守其 GPLv2+ 与字体嵌入例外，详见 `fonts/LICENSE-UNIFONT.txt`。`include/fbink.h` 来自 NiLuJe/FBInk，仅作编译期头文件使用。接口常量与 selector 参考 GreenSkinMonster/hipda（Android 客户端）。
