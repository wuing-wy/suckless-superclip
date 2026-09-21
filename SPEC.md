# superclip 规格说明

状态：Draft 1  
目标平台：Linux / X11  
实现语言：C99

本文中的“必须”“不得”“应该”和“可以”分别表示强制要求、禁止要求、推荐要求和可选能力。

## 1. 项目定位

`superclip` 是一个由键盘驱动的 X11 命令面板，也是一个轻量的扩展前端。启动后，它在当前显示器中央创建输入窗口；用户选择扩展、输入查询、选择一条纯文本结果并执行。

`superclip` 遵循以下原则：

- 核心只负责窗口、输入、结果展示、扩展发现及进程通信。
- 应用搜索、文件搜索、计算、shell、剪贴板历史等业务能力必须由扩展实现。
- 未被选择的扩展不得因普通 GUI 启动而执行。
- 核心不常驻、不保存业务状态，也不管理跨会话 daemon。
- 不因“以后可能需要”而预先加入框架、协议字段或依赖。
- 优先采用小型、明确、可替换的 Unix 进程协议，而非动态链接插件 ABI。
- 不设脱离实际环境的二进制大小或 RSS 指标；依赖数、常驻进程数、代码量和无用工作本身是需要控制的成本。

## 2. 范围

### 2.1 首版必须提供

- X11 中央输入窗口和纯文本候选列表。
- 用户扩展和系统扩展的发现。
- 短进程扩展与会话持久扩展。
- `enter` 和 `change` 两种查询触发策略。
- UTF-8、XIM 和完整 Unicode 字素簇编辑。
- Xinerama 多显示器定位。
- 编译期 `config.h` 配置。
- `superclip setup` 只读安装检查。
- 一个默认安装的纯文本剪贴板历史扩展及其独立 daemon。

### 2.2 明确不做

- Wayland。
- GTK、Qt、Cairo 或浏览器式 UI。
- 鼠标交互、触摸交互和拖放。
- 图标、图片、缩略图、Markdown、ANSI 样式、富文本或预览面板。
- 多层视图、表单或扩展自定义 UI。
- 核心内建模糊搜索、排序、索引、shell 执行或剪贴板历史。
- 运行时 TOML、JSON、YAML 或 INI 配置。
- Raycast 协议兼容。若未来需要，必须作为独立扩展或新协议版本设计，不能污染 v1 核心。
- 核心 daemon、核心自动启动项、DBus 服务或 systemd 用户服务管理。

## 3. 依赖与构建

### 3.1 核心依赖

- C99 编译器与 POSIX.1-2008 环境。
- Xlib (`libX11`)。
- Xft 与 fontconfig。
- Xinerama。
- `libgrapheme`，用于符合 Unicode 标准的字素簇分段。

核心不得依赖 GTK、Qt、JSON 库、脚本运行时或通用插件框架。

### 3.2 剪贴板组件的额外依赖

- XFixes (`libXfixes`)，只由剪贴板 daemon 使用。

### 3.3 构建形式

项目必须提供简单的 POSIX `make` 构建。推荐采用 suckless 风格文件：

```text
config.def.h
config.h
config.mk
Makefile
```

首次构建可由 `config.def.h` 生成未被版本控制覆盖的 `config.h`。字体、颜色、尺寸、边距、结果行数、按键、防抖时长和本地资源上限均在 `config.h` 中配置；修改后重新编译。

## 4. 安装布局

以默认 `PREFIX=/usr/local` 为例：

```text
/usr/local/bin/superclip
/usr/local/bin/superclip-clipboardd
/usr/local/libexec/superclip/extensions/clipboard
```

扩展从以下目录按优先级发现：

1. `$XDG_CONFIG_HOME/superclip/extensions/`
2. 若 `XDG_CONFIG_HOME` 未设置：`$HOME/.config/superclip/extensions/`
3. 编译期的 `$PREFIX/libexec/superclip/extensions/`

用户目录中的同名扩展覆盖系统扩展。不得扫描 `$PATH`。

每个目录只扫描第一层，不递归。允许符号链接，但其最终目标必须是可执行的普通文件。扩展名必须匹配：

```text
[A-Za-z0-9][A-Za-z0-9._-]*
```

名称 `setup` 保留给核心，不得作为扩展名。

## 5. 命令行接口

### 5.1 打开扩展选择器

```sh
superclip
```

窗口列出已发现的扩展名。此时不得执行任何扩展，也不得为了取得友好名称批量调用自描述接口。选择扩展后才加载该扩展。

### 5.2 直接进入扩展

```sh
superclip <subcommand> [initial query ...]
```

示例：

```sh
superclip calc
superclip calc 1+2
superclip clipboard hello
```

`subcommand` 精确选择扩展。余下参数以单个 U+0020 空格连接，成为输入框的初始查询。调用仍然打开 GUI，不提供隐式 headless 查询模式。

### 5.3 安装检查

```sh
superclip setup
```

这是纯命令行、只读的内建命令。它可以执行所有已安装扩展的自描述与 setup 接口，并汇总：

- 扩展是否可用。
- 是否需要后台服务。
- 建议启动的命令。
- `.xinitrc` 或 dwm autostart 示例。
- 扩展提供的状态检查结果。

`setup` 不得写配置、创建启动项、安装 service、启动 daemon 或修复系统。

## 6. 进程生命周期

- `superclip` 不常驻，也不自行注册全局快捷键。
- 用户应通过 dwm、sxhkd 或其他 X11 快捷键工具启动它。
- 窗口关闭或动作成功后，`superclip` 退出。
- 多次调用可以产生多个独立实例；核心不实现单实例 daemon。
- 会话持久扩展只表示其协议进程可以在当前窗口存活期间处理多个请求。
- 扩展若需要跨窗口、跨登录会话的 daemon，必须自行实现和管理；该 daemon 不属于核心协议生命周期。
- UI 退出时，核心向会话扩展发送 `QUIT` 并关闭管道。会话扩展必须在 EOF 后退出，不得通过继承的标准输入输出阻止核心退出。
- 核心不得自动启动、监控或停止扩展自己的跨会话 daemon。

## 7. X11 窗口与输入

### 7.1 窗口

- 使用 Xlib 创建无装饰的 `override_redirect` 窗口。
- 程序自行定位、绘制、置顶并抓取键盘。
- 键盘抓取失败必须被明确处理，不得悄悄显示一个无法输入的窗口。
- 不依赖窗口管理器规则。

### 7.2 多显示器定位

使用 Xinerama 选择目标显示器：

1. 当前键盘焦点窗口所在的显示器。
2. 无法确定时，鼠标指针所在的显示器。
3. 仍无法确定时，X11 默认屏幕。

窗口位于目标显示器工作区域中央。首版无需支持运行中跨屏移动。

### 7.3 文本输入

- 使用 XIM 接收国际化输入。
- 内部文本、Xft 绘制和扩展协议必须使用合法 UTF-8。
- 非法 UTF-8 不得进入输入缓冲区。
- 光标移动、退格和删除必须以完整 Unicode 字素簇为单位。
- 使用 `libgrapheme`，不得把 Unicode 码点或 UTF-8 字节误当作用户感知字符。
- 通过 fontconfig 查找缺失字符的后备字体，并只在当前 UI 会话中缓存打开的 Xft 字体。
- 支持从 X11 `CLIPBOARD` 粘贴 UTF-8 文本；默认快捷键为 `Ctrl-v`。不处理富文本。

### 7.4 最小按键集合

默认必须支持：

| 按键 | 行为 |
|---|---|
| `Left` / `Right` | 按字素簇移动光标 |
| `Home` / `End` | 移至输入开头/结尾 |
| `Backspace` / `Delete` | 删除完整字素簇 |
| `Up` / `Down` | 选择上一项/下一项 |
| `Ctrl-p` / `Ctrl-n` | 选择上一项/下一项 |
| `Ctrl-v` | 从 `CLIPBOARD` 粘贴 |
| `Enter` | 发起查询、选择扩展或执行当前结果，取决于状态 |
| `Escape` | 立即关闭窗口 |

首版不处理鼠标点击、滚轮或悬停。

## 8. UI 状态与查询模型

### 8.1 状态

核心至少具有以下逻辑状态：

```text
EXTENSION_SELECT
QUERY_EDIT
QUERY_RUNNING
RESULT_SELECT
ERROR
```

UI 始终为单输入框加平面候选列表。结果只有 `title` 和可选 `description` 两行纯文本字段。

### 8.2 扩展运行模式与触发方式

扩展自描述以下两个正交属性：

| 进程模式 | 触发方式 | 是否有效 | 行为 |
|---|---|---:|---|
| `short` | `enter` | 是 | 用户按 Enter 时启动一次查询进程 |
| `short` | `change` | 否 | v1 明确禁止 |
| `persistent` | `enter` | 是 | 会话进程常连，按 Enter 才查询 |
| `persistent` | `change` | 是 | 输入停止变化并经过防抖后查询 |

`change` 默认防抖为 50 ms，可在核心 `config.h` 中修改。

### 8.3 短进程交互

```text
编辑查询 -> Enter -> 启动短查询进程 -> 显示候选
显示候选 -> 选择 -> Enter -> 启动短执行进程 -> 成功后关闭
```

候选出现后，如果用户重新编辑查询，核心必须立即清空旧候选并回到等待 Enter 的 `QUERY_EDIT`，不得自动重新查询。

### 8.4 持久进程交互

- `persistent + enter` 与短进程具有相同的显式查询体验，但复用当前会话管道。
- `persistent + change` 在防抖后发送新查询。
- 每个查询都有递增 `request-id`。新请求发出后，旧 ID 立即过期；其后到达的旧结果必须继续被读取消耗，但不得展示。
- 核心不规定扩展内部使用串行、异步还是取消计算。

### 8.5 结果和动作

- 扩展负责过滤、排序、相关性和去重。
- 核心严格保持扩展输出顺序，不做二次搜索或重排。
- 结果 ID 对核心是不透明 UTF-8 字符串。
- 用户按 Enter 后，由扩展执行该结果对应的动作。
- 动作成功后关闭窗口；失败则保留窗口并显示一行错误。
- v1 不支持动作执行后进入下一层视图。

## 9. 扩展协议 v1

### 9.1 总则

- 协议使用 UTF-8、逐行记录、Tab 分隔字段。
- 每条记录以 LF 结束；不得使用 NUL。
- 字段使用以下转义：`\\`、`\t`、`\n`、`\r`。
- 未定义的转义、字段数错误、非法 UTF-8 和未知记录类型均为协议错误。
- 扩展不得向协议 stdout 写日志；诊断写 stderr。
- 核心和扩展必须忽略已经过期但格式合法的 `request-id` 响应。
- 协议版本不兼容时必须拒绝扩展，不做猜测性降级。

### 9.2 协议硬上限

v1 的上限是协议的一部分：

- 单条编码后记录不得超过 65,536 字节（包含 LF）。
- 单个查询最多接受 256 条 `ITEM`。
- 超出上限、缺少终止记录或格式错误时，当前扩展会话失败。
- 实现可以在 `config.h` 中设置更低的本地上限，但不得宣称支持高于 v1 的值。
- 达到候选条数上限后，核心仍必须排空当前响应直到 `END`，以保持管道同步。

### 9.3 自描述

核心执行：

```sh
<extension-path> --superclip-describe
```

扩展必须输出且只输出一条：

```text
SUPERCLIP<TAB>1<TAB>name<TAB>process-mode<TAB>trigger<TAB>display-name
```

示例：

```text
SUPERCLIP\t1\tclipboard\tpersistent\tchange\tClipboard history
```

`name` 必须与发现目录中的文件名一致。`process-mode` 为 `short` 或 `persistent`；`trigger` 为 `enter` 或 `change`。`short + change` 必须拒绝。

普通 GUI 只对用户已经选择的扩展调用此接口。`superclip setup` 是允许遍历所有扩展的显式例外。

### 9.4 setup 接口

核心执行：

```sh
<extension-path> --superclip-setup
```

允许的输出记录为：

```text
NONE
AUTOSTART<TAB>label<TAB>command<TAB>description
STATUS<TAB>ok|warning|error<TAB>message
```

`command` 只是向用户显示的说明文本，核心不得执行或交给 shell。无需后台服务的扩展返回 `NONE`。

### 9.5 短查询

核心启动：

```sh
<extension-path> --superclip-query
```

然后向 stdin 写入一条请求并关闭写端：

```text
QUERY<TAB>request-id<TAB>query
```

扩展向 stdout 返回：

```text
BEGIN<TAB>request-id
ITEM<TAB>request-id<TAB>result-id<TAB>title<TAB>description
ITEM<TAB>request-id<TAB>result-id<TAB>title<TAB>description
END<TAB>request-id
```

无结果时仍必须输出 `BEGIN` 和 `END`。发生可展示错误时，可以在 `BEGIN` 前返回：

```text
ERROR<TAB>request-id<TAB>message
```

### 9.6 短动作执行

核心重新启动：

```sh
<extension-path> --superclip-execute
```

并向 stdin 写入：

```text
EXECUTE<TAB>request-id<TAB>query<TAB>result-id
```

同时传回原查询，使短进程扩展无需依赖已经退出的查询进程状态。扩展返回其一：

```text
OK<TAB>request-id
ERROR<TAB>request-id<TAB>message
```

### 9.7 持久会话

核心启动：

```sh
<extension-path> --superclip-session
```

stdin 可以包含多条：

```text
QUERY<TAB>request-id<TAB>query
EXECUTE<TAB>request-id<TAB>query<TAB>result-id
QUIT
```

扩展使用与短模式相同的 `BEGIN`、`ITEM`、`END`、`OK` 和 `ERROR` 响应。多个请求的响应可以交错，但每条都必须带正确的 `request-id`。收到 `QUIT` 或 EOF 后，会话进程必须退出。

### 9.8 进程与安全要求

- 核心只使用已解析的绝对扩展路径配合 `fork()` 和 `execve()`。
- 禁止 `system()`、`popen()` 和 `/bin/sh -c`。
- 查询、result ID 和 setup 文本不得拼接成 shell 命令。
- 若用户需要执行 shell，必须安装专门扩展；核心不为它提供特殊权限。
- 扩展管道必须非阻塞。核心使用单线程 `poll()` 同时处理 X11 fd、子进程管道和退出状态。
- 无论扩展是否响应，用户都必须能按 Escape 退出。
- stderr 仅作诊断。核心可以捕获有界的首段 stderr；若进程异常退出且没有协议 `ERROR`，只显示通用错误和首行诊断。
- 核心不实现日志框架，也不得无限缓存 stderr。

## 10. 剪贴板扩展

### 10.1 组件

默认提供：

```text
clipboard                 系统扩展，persistent + change
superclip-clipboardd      用户显式管理的前台 daemon
```

扩展通过用户私有 Unix domain socket 与 daemon 通信。核心不知道此私有协议，也不管理 daemon。

`superclip-clipboardd` 默认以前台方式运行；用户可以在 `.xinitrc` 或 dwm autostart 中显式写：

```sh
superclip-clipboardd &
```

扩展和核心不得因打开窗口而自动启动 daemon。daemon 未运行时，扩展返回清晰错误。`superclip setup` 显示启动建议和状态。

### 10.2 socket 与单实例

- 首选 `$XDG_RUNTIME_DIR/superclip/clipboard.sock`。
- 若 `XDG_RUNTIME_DIR` 未设置，可使用 `/tmp/superclip-$UID/clipboard.sock`，但必须创建并验证父目录由当前 UID 所有且权限为 `0700`。
- socket 必须仅允许当前用户访问。
- 同一用户和 DISPLAY 只允许一个 daemon；重复启动必须明确失败。
- 私有 IPC 必须有长度上限，不得信任客户端长度字段。

### 10.3 监听语义

- daemon 使用 XFixes 监听 `CLIPBOARD` selection owner 变化，不做定时轮询。
- 启动时尝试读取当时的当前 `CLIPBOARD`。
- 只记录文本：首选 `UTF8_STRING`，必要时回退 `STRING`。
- 必须正确处理 ICCCM `INCR` 增量传输，并在达到单条大小上限时中止。
- 忽略 `PRIMARY`、`SECONDARY`、图片、HTML、文件列表和其他二进制 target。
- X11 selection 是异步 owner 模型。本项目只承诺 best-effort 捕获，不承诺极快 owner 切换、源应用提前退出或不合规应用下的零遗漏。

### 10.4 历史策略

剪贴板 daemon 使用自己的编译期配置，默认：

- 忽略空文本。
- 记录前移除首尾 Unicode `White_Space` 字符；中间内容保持不变。裁剪后为空的文本按空文本忽略。
- 连续相同内容只保留一条，并移动为最新条目。
- 单条文本最多 256 KiB；超限时整条忽略，不截断。
- 最多保存 200 条。
- 持久化文件最多 8 MiB；压缩时删除最旧记录。
- 查询结果按最新优先返回；过滤和排序完全由该扩展实现。

### 10.5 持久化

历史存储于：

```text
$XDG_STATE_HOME/superclip/clipboard
```

若 `XDG_STATE_HOME` 未设置，使用：

```text
$HOME/.local/state/superclip/clipboard
```

要求：

- 目录权限不宽于 `0700`，历史文件权限为 `0600`。
- 采用有界追加格式，不引入 SQLite。
- 压缩使用同目录临时文件、完整写入后原子 rename，避免崩溃破坏现有历史。
- 历史明文保存。文档必须警告其中可能包含密码、token 等敏感信息。
- 本项目不实现历史加密或密钥管理；静态加密由加密家目录或磁盘负责。

### 10.6 选择历史项

用户执行一条历史结果时：

1. `clipboard` 扩展把条目 ID 发送给 daemon。
2. daemon 读取完整文本并取得 `CLIPBOARD` 所有权。
3. daemon 响应后续 selection 请求，使文本在扩展与 `superclip` 退出后仍可粘贴。
4. daemon 必须识别自己设置 selection 引起的 XFixes 事件，避免重复记录或事件循环。
5. 成功后扩展返回 `OK`，主窗口关闭。

### 10.7 管理命令

```sh
superclip-clipboardd status
superclip-clipboardd clear
```

- `status` 通过私有 socket 报告运行状态、条目数和状态文件位置。
- `clear` 请求运行中的 daemon 清空内存与磁盘历史。
- 两者不得隐式启动第二个 daemon。
- 首版不提供逐条删除、暂停记录或图形设置页。

## 11. 错误处理

- 扩展不存在、不可执行、自描述非法或协议版本不兼容时，GUI 显示一行明确错误，Escape 仍可退出。
- 扩展不得让 X11 事件循环阻塞。
- 超长记录、过多候选、非法 UTF-8、未知响应、错误 request ID 或缺少 `END` 都是当前扩展会话错误。
- 子进程必须通过 `waitpid()` 回收，不得产生 zombie。
- GUI 退出时先关闭协议写端；不合作的会话子进程可以依次收到 `SIGTERM` 和 `SIGKILL`，等待时间为编译期小常量。
- X server 断开、键盘抓取失败和无法创建字体属于核心致命错误，应向 stderr 输出原因并以非零状态退出。
- 单个缺字字体、单条扩展错误或 daemon 未运行不应使整个 X client 崩溃。

## 12. 代码组织原则

推荐按职责拆分少量 C 文件，而不是建立通用对象系统：

```text
main.c          参数解析与顶层状态机
x11.c           窗口、事件、XIM、Xft、Xinerama
text.c          UTF-8 缓冲区与字素簇编辑
extension.c     发现、fork/exec、非阻塞协议
protocol.c      v1 转义、解析和边界检查
util.c          小型通用辅助函数
clipboard/      扩展与 daemon
```

约束：

- 不建立扩展 C ABI，不使用 `dlopen()`。
- 不实现通用 widget toolkit。
- 不加入未被当前规格使用的抽象层。
- 所有动态缓冲区必须有显式上限和溢出检查。
- 优先使用清晰数据结构和显式状态枚举，避免宏技巧隐藏控制流。
- 第三方扩展可以使用任意语言；核心不因此捆绑对应运行时。

## 13. 测试与验收

### 13.1 单元测试

- 协议字段转义和反转义。
- 非法 UTF-8、NUL、超长记录和候选上限。
- request ID 过期与交错响应。
- Unicode 字素簇移动和删除，包括组合字符、ZWJ emoji、旗帜和肤色修饰符。
- 扩展目录优先级、符号链接、保留名和非法文件名。
- 剪贴板历史去重、上限、压缩与损坏恢复。

### 13.2 集成测试

使用 Xvfb 和可控假扩展验证：

- `superclip` 启动时不执行未选择扩展。
- `superclip <subcommand>` 直接进入正确扩展。
- 三种有效模式组合的查询时机。
- 50 ms 默认防抖和旧 request ID 丢弃。
- 短查询重新编辑后不会自动运行。
- 动作成功关闭、动作失败保留窗口。
- 扩展挂起时 Escape 仍能退出。
- 子进程被回收且无后台核心进程残留。
- 用户扩展覆盖同名系统扩展。
- `setup` 只输出建议，不产生系统修改。
- clipboard daemon 不运行时错误清晰。
- clipboard daemon 能捕获文本、跨 GUI 会话保留历史、重新取得 selection 所有权并清空历史。

### 13.3 完成定义

首版完成必须同时满足：

1. 核心所有行为均可在无 GTK/Qt 的 X11 环境运行。
2. 关闭最后一个窗口后，不残留 `superclip` 核心进程或会话扩展进程。
3. 未选择的扩展不会在普通 GUI 流程中被执行。
4. 核心只展示纯文本结果，不解释 result ID 或执行 shell。
5. 协议输入、查询缓冲区、候选列表、stderr 捕获和剪贴板历史全部有界。
6. UTF-8 输入和完整字素簇编辑通过测试。
7. 随项目安装的剪贴板扩展遵守与第三方扩展相同的公开协议；不得获得核心私有入口。

## 14. 后续能力的准入规则

Wayland、图标、更多动作、预览、多层视图、运行时配置、模糊搜索或 Raycast 兼容都不是预留字段。未来只有在存在明确使用场景，且能证明不能由普通扩展独立完成时，才讨论新的核心能力；破坏协议的变化必须提升协议主版本。
