# SPEC: snippets 扩展

> Status: `draft` — Date: 2026-09-24 — Author: agent + user

## 1. Background

用户要一个 `snippets` 系统扩展：自定义文本片段存纯文本文件，面板按输入实时过滤，选中后把片段设到 X11 `CLIPBOARD`，面板退出后仍可粘贴。

依据的 `.code-reading` 笔记：`extension-transport`（fork+execve、三管道、QUIT 语义）、`clipboard-history`（daemon 拥有 selection 并常驻服务的模式、私有路径规则）、`cli-and-lifecycle`（persistent+change 的 debounce/QUERY 复用、`draw` 只列名）、`docs/EXTENSIONS.md` + `docs/PROTOCOL.md`（v1 记录格式、转义集、三种合法模式组合）。

约束（`AGENTS.md`，本次无新增约定）：Linux/X11 only；C99 + POSIX make；核心依赖外扩展仅用 libc/POSIX + Xlib（不用 XFixes/GTK/Qt/Cairo/JSON/DBus/脚本运行时）；禁 `system()/popen()/sh -c/dlopen()`；片段内容永不执行；单线程；所有输入/缓冲有界；诊断走 stderr；用户配置为编译期 `config.h`，另加一条经用户决策的显式例外：片段文件 `$XDG_CONFIG_HOME/superclip/snippets` 属用户自有扩展数据（类比剪贴板历史 daemon 状态），以固定界（REQ-3）在运行时解析，无通用配置语言（无 TOML/JSON/YAML），不旁路 `config.h` 的 UI/行为开关。

## 2. Goals / Non-goals

- Goals:
  - `snippets` 扩展：`persistent + change`，C 实现，与第三方扩展走同一公开协议，无核心私有入口。
  - 片段文件：`$XDG_CONFIG_HOME/superclip/snippets`，未设则 `$HOME/.config/superclip/snippets`；行格式 `标题<TAB>转义正文`，转义集 `\\ \t \n \r`（与协议同构），空查询列全部、非空子串过滤（大小写敏感，标题或正文命中）。
  - 选中执行：扩展内 fork holder 常驻进程直接拥有 X11 `CLIPBOARD` 并单次响应粘贴请求；面板/会话退出后仍可粘贴；单实例（新执行替换旧 holder）+ 显式退出方式。
- Non-goals:
  - 不做模糊搜索/排序/去重（文件顺序原样返回，核心亦不重排）。
  - 不做 TOML/JSON/YAML 通用配置语言、多行 raw 行、逐条删除 UI、暂停记录、加密（明文配置，与剪贴板历史同等警告）。
  - 不经 `clipboardd` 拥有（本次用户决策：直控 selection + fork holder）；不写 daemon 历史（holder 拥有引发 daemon best-effort 捕获属预期副作用，仅文档说明）。
  - 不引入新依赖、线程、通用运行时配置解析、鼠标/图像行为。

## 3. Requirements

- [ ] REQ-1 (describe/setup): `--superclip-describe` 输出且仅输出 `SUPERCLIP\t1\tsnippets\tpersistent\tchange\tSnippets`；`--superclip-setup` 输出 `NONE`。
- [ ] REQ-2 (session): 实现 `--superclip-session`，顺序处理 `QUERY/EXECUTE`，`QUIT`/EOF 即退出；同时兼容 `--superclip-query/--superclip-execute` 单次模式（与 clipboard-extension 同构）。
- [ ] REQ-3 (文件与解析): 缺文件视为有效空集（`BEGIN/END` 零 `ITEM`）；每次 `QUERY`/`EXECUTE` 重新读取（无缓存，保证编辑即生效）。行规则：
  - 空行、全空白行、前导空白后 `#` 开头行静默跳过（`#` 须为行首非空白首字符才算注释）；
  - 按首个 raw `TAB` 切分，无 `TAB`、转义非法、标题解码空/含 `\t\n\r`、标题解码 >512B、正文解码 >65536B、raw 行 >131072B 的行计为坏行并跳过（每请求至多一行 stderr 汇总 `snippets: skipped N bad lines\n`，N=0 不输出），不使会话失败；
  - 文件上限 1MiB、扫描上限 1024 行、保留前 256 个有效片段（文件序）。
- [ ] REQ-4 (查询): 空查询返回全部（≤256）；非空查询对解码后标题/正文做大小写敏感子串匹配。排序：最近执行区在前（会话内存，最多 8 个 `result-id`，只存 ID 不存快照；命中即移到最前，去重；文件改了导致 ID 失效则该项跳过），随后文件序。`ITEM` 记录：`ITEM\t<reqid>\t<result-id>\t<标题>\t<预览>`，行号为 1-based 文件行号（跳过的行也占号，便于定位）。`result-id` 为不透明复合串 `<行号>:<标题>`（十进制行号 + 首个 `:` + 解码后标题原文；标题可含任意除 NUL 外 UTF-8，线上传输时按协议转义，核心原样回传无需改动）。预览取解码后正文按 `\n` 切首行、按码点边界截断 120 字节（永不拆散 UTF-8 序列；扩展仅 libc+Xlib，不链 libgrapheme，字素簇安全不要求）。`EXECUTE` 回传该 `result-id`，扩展拆出号与标题做重校验（TOCTOU 防护，见 REQ-5）。
- [ ] REQ-5 (执行与 holder): `EXECUTE`（v1 四字段 `EXECUTE\t<exec-id>\t<query>\t<result-id>`，核心/协议零改动）拆解不透明 `result-id` 为行号与标题，重读文件查找该行有效片段并比对标题一致（QUERY-EXECUTE 间文件被改即标题不符），未知/失效行号或标题不符回 `ERROR\t<exec-id>\tunknown snippet`；标题中 `:` 按首个切分，行号须全数字且 ≥1。选中即记最近使用（见 REQ-4 recent 区，拥有失败也记）。拥有确认后再回包：`EXECUTE` 处理方经有界握手（`pipe()` + 1 字节 ready/fail + ≤1s 超时）等 holder 确认 `XSetSelectionOwner` 成功才回 `OK\t<exec-id>`，否则回 `ERROR\t<exec-id>\tcannot own clipboard`；永不以 fork 成功代替拥有成功（SPEC §8.5 成功关面板/失败留面板）。holder 要求：
  - 会话进程 `fork` 中间子后立即 `waitpid` 回收（中间子 `_exit(0)`），由孙辈 `setsid` 脱离（单 fork + setsid 等价实现亦可，关键是无僵尸，见 SPEC §11）；关闭继承的协议 stdin 读端/stdout-stderr 写端与多余 fd（三管道不留），重定向 `/dev/null`，不得阻止核心退出；握手 `pipe` 写端在两次 fork 间保持打开，其余 `pipe` 端一律关闭；
  - 打开自有 Display，单线程 `poll(XConnectionNumber)+XNextEvent` 循环（`poll` 200ms 超时或 `EINTR` 唤醒检查 `volatile sig_atomic_t` 旗标；Xlib 调用只在主循环）；`XSetSelectionOwner` 拥有 `CLIPBOARD` 后以 `XGetSelectionOwner` 回查确认（握手成功条件）；通告 `TARGETS={TIMESTAMP,TARGETS,UTF8_STRING,STRING}`；`TARGETS/UTF8_STRING/STRING` 单次 `XChangeProperty`，`TIMESTAMP` 回尽力而为的 server-time 近似值（`XChangeProperty` 32 位单值）；`INCR/MULTIPLE/SAVE_TARGETS` 以 `SelectionNotify.property=None` 明确拒绝；`SelectionClear`（失去拥有）或 `SIGTERM`（经旗标唤醒）即退出，干净退出必 `unlink` pid 文件；
  - 单实例替换（六步）：(1) 按 REQ-6 计算 pid 路径；(2) 读 `pid + starttime`，存活且 starttime 一致才 `SIGTERM`，`SNIPPETS_HOLDER_GRACE_MS 200` 内轮询至退出否则 `SIGKILL`；(3) 死 pid/starttime 不一致/解析失败判 stale（`unlink` 不 kill；解析失败不单独覆盖，须叠加连接失败才推进，避免 PID 复用双 holder）；(4) holder 允许创建运行时父目录（0700/UID 校验）与 pid 文件（0600，`O_CREAT|O_EXCL` 直接原子写 `pid + starttime`，并发以先胜者为准，败者回 `cannot own clipboard`），但永不创建用户配置片段文件、不写 autostart；(5) 新 holder 启动前重复 (2)；(6) 干净退出 `unlink`，`--holder-stop` 幂等；`DISPLAY` 未隔离（与 clipboard 同类 gap，holder 全局有效，README 必须写跨屏抢占警告）；
  - 显式退出：扩展绝对路径 `--holder-stop`（如 `/usr/local/libexec/superclip/extensions/snippets --holder-stop` 及用户目录覆盖件同名路径；`argc==2` 精确匹配，否则 exit 2；幂等：已停/无 holder 回 exit 0，仅不可恢复错误回 exit 1 + 单行 stderr）与拥有丢失自退。
- [ ] REQ-6 (有界、路径与错误): pid 路径：`$XDG_RUNTIME_DIR/superclip/snippets-holder.pid`（`XDG_RUNTIME_DIR` 为空视作未设则用 `/tmp/superclip-$UID/`）；父目录须 0700 且属当前 UID；片段路径：`$XDG_CONFIG_HOME/superclip/snippets`（为空视作未设则用 `$HOME/.config/superclip/snippets`）。协议上限沿用 v1（单记录 65536B；扩展侧截断到文件序前 256 个有效命中并必发 `END`，>256 的 drain 到 `END` 属核心侧规则）；所有动态缓冲显式上限；回包 id 规则：`BEGIN/ITEM/END` 回显其所属 `QUERY` 的 id，`OK/ERROR` 回显其所属 `EXECUTE` 的 id，永不混用；stderr 仅诊断。
- [ ] REQ-7 (构建安装): `make` 产出 `snippets-extension`（`snippets/extension.c` 单源，窄链接 `pkg-config --libs x11`，不带 Xft/XFixes/fontconfig/Xinerama/libgrapheme），`make install` 装到 `$(LIBEXECDIR)/snippets`，`make clean/uninstall` 覆盖；无新增依赖。

Assumed: 无。120B/512B/65536B/131072B/1MiB/1024/256 均为 REQ-3/REQ-4 的规范界（SPEC.md 未覆盖扩展数据文件，本 SPEC 新定）；行读取器自身亦以 raw 131072B 为界，拒绝无界 `getline`。holder 对 `INCR` 请求明确拒绝（见 REQ-5），非静默失败。

## 4. Design

接口（协议行均 LF 结尾、Tab 分隔、协议转义）：

```text
QUERY<TAB>request-id<TAB>query
  -> BEGIN<TAB>request-id
     ITEM<TAB>request-id<TAB>result-id<TAB>title<TAB>preview ... (<=256, 文件序)
     END<TAB>request-id
EXECUTE<TAB>exec-id<TAB>query<TAB>result-id（result-id=`<行号>:<标题>` 不透明复合，见 REQ-4）
  -> OK<TAB>exec-id | ERROR<TAB>exec-id<TAB>message
QUIT / EOF -> exit 0
--superclip-describe -> SUPERCLIP 1 snippets persistent change Snippets（恰好一行，LF 结尾）
--superclip-setup    -> NONE（恰好一行，LF 结尾）
<ext-path> --holder-stop -> exit 0（已停/无 holder 幂等）/1（不可恢复）/2（argv 非法）+ 单行 stderr
```

片段文件示例（`$XDG_CONFIG_HOME/superclip/snippets`）：

```text
# title<TAB>body，body 转义：\\ \t \n \r
deploy\tkubectl rollout restart deploy/api\nkubectl rollout status deploy/api
daily\tToday I did: 
```

错误契约：缺文件（`ENOENT`）= 空集（`BEGIN/END` 零 `ITEM`）；`EACCES`/文件 >1MiB 按请求回固定串 `ERROR\t<id>\tcannot read snippets` / `ERROR\t<id>\tsnippets file too large`，会话存活；坏行跳过且每请求至多一行 stderr 汇总 `snippets: skipped N bad lines\n`（N=0 不输出），永不走 stdout；未知 result-id/标题不符回 `unknown snippet`；holder 拥有失败回 `cannot own clipboard`；stdin 协议级错误（非法 UTF-8/转义/字段数）按 v1 会话失败。文件错误固定串与 `snippets: skipped` 汇总串为测试断言依据。

单实例替换算法（pid 文件 `$XDG_RUNTIME_DIR/superclip/snippets-holder.pid`，`XDG_RUNTIME_DIR` 为空视作未设则用 `/tmp/superclip-$UID/`；父目录 0700 且属当前 UID 校验，pid 文件 0600；空 `XDG_CONFIG_HOME` 同视作未设，不扫描 `$PATH`，扩展不自动建文件/目录；宽限常量独立定义 `SNIPPETS_HOLDER_GRACE_MS 200`，不复用核心 `SUPERCLIP_CHILD_GRACE_MS`）：读 pid → `kill(pid,0)` 存活则 `SIGTERM`，`SNIPPETS_HOLDER_GRACE_MS` 内轮询至退出否则 `SIGKILL`；僵死/PID 复用（`/proc/<pid>/starttime` 对不上或解析失败）与 `ENOENT` 同按 stale 处理（`O_CREAT|O_EXCL` + 原子写）；`DISPLAY` 未隔离（与 clipboard 同类，holder 全局有效），文档明示。

短模式语义：`--superclip-query/--superclip-execute` 单次处理一条请求（半关语义由核心侧 `close_after` 保证，扩展读到 EOF 即知结束）；holder detach 与 pid 替换逻辑与会话模式同一代码路径；stdin 解析失败 exit 1（同 `clipboard/extension.c` 约定）。

安全说明：片段文件为明文用户数据，可能含密码/token，`snippets/README.md` 必须给出同剪贴板历史同等的明文警告；组/他可读文件每进程对同一路径警告一次（stderr），不拒绝服务。holder 每次拥有会被 clipboard daemon best-effort 捕获追加进剪贴板历史（daemon 只忽略自有窗口），属预期副作用，README 必须明示且 Xvfb 测试断言历史增长；如用户不可接受则不应使用直控拥有路径。


Affected files（来自 `.code-reading`）: 新增 `snippets/extension.c`（含 holder，单文件、单二进制，镜像 `clipboard/extension.c` 结构：`read_line/handle_stdin/main` + `load_snippets/filter/respond_query/respond_execute/spawn_holder/holder_main`）、`snippets/SPEC.md`（本文件）、`snippets/README.md`（用户文档：文件格式/示例/`--holder-stop`）；改 `Makefile`（`snippets-extension` 构建/安装/清理）、`tests/test_snippets.c` + `tests/run_snippets_xvfb.sh`（测试）；`.code-reading/features/snippets.md` + `INDEX.md` + `PROJECT-MAP.md`（对应性）。

## 5. Test Plan

| Test item | Location | Covers | Expected |
|-----------|----------|--------|----------|
| 解析：转义/坏行跳过/标题空拒绝/超限拒绝/注释空行/`#` | `tests/test_snippets.c` | REQ-3 | passes |
| 过滤：空查全量子串/大小写/recent 在前去重/文件序/256 截断 | `tests/test_snippets.c` | REQ-4 | passes |
| result-id=行号：EXECUTE 命中/未知行号 ERROR/QUERY-EXECUTE 间改文件不串片（标题重校验） | `tests/test_snippets.c` + Xvfb | REQ-4/REQ-5 | passes |
| Xvfb：QUERY 列出/EXECUTE 后会话退出但 `--request` 读回一致（证 holder 而非 session 拥有） | `tests/run_snippets_xvfb.sh` | REQ-5 | passes |
| holder：会话退出后仍可粘贴；二次执行 PID 变化且旧 PID 已死（替换）；`--holder-stop` 后无进程 + pid 文件清 + owner 为 None | `tests/run_snippets_xvfb.sh` | REQ-5 | passes |
| describe/setup 精确字节（含尾 LF、无多余记录） | `tests/test_snippets.c` | REQ-1 | passes |

## 6. How to Build & Use

- Build: `make`（新增 `snippets-extension`）；`make install` 装扩展到 `$PREFIX/libexec/superclip/extensions/snippets`；测试见 Test Plan。
- Use: 建 `mkdir -p "${XDG_CONFIG_HOME:-$HOME/.config}/superclip"` 后写 `snippets` 文件（上例格式）；`superclip snippets` 打开，输入过滤、Enter 查询（change 模式自动）、选中 Enter 即拥有到 CLIPBOARD，面板关闭后粘贴；`<ext-path> --holder-stop`（如 `/usr/local/libexec/superclip/extensions/snippets --holder-stop`）显式停 holder。明文警告：片段文件与剪贴板历史同为明文，勿存密码/token；每次拥有会被 clipboard daemon 捕获追加进历史。
- NOT verified: 按本 skill Step 5，agent 不在本地全量构建；以上命令由用户/CI 执行验证。
