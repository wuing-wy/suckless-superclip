# .code-reading INDEX

> Code ref: `f654dde` — Updated: 2026-09-24

| Feature | Note | Files | Status |
|---------|------|-------|--------|
| cli-and-lifecycle | `features/cli-and-lifecycle.md` | `main.c` | draft |
| x11-frontend | `features/x11-frontend.md` | `x11.c`, `x11.h` | draft |
| input-editing | `features/input-editing.md` | `text.c`, `text.h`, `util.c`, `util.h` | draft |
| protocol-codec | `features/protocol-codec.md` | `protocol.c`, `protocol.h` | draft |
| extension-transport | `features/extension-transport.md` | `extension.c`, `extension.h` | draft |
| clipboard-history | `features/clipboard-history.md` | `clipboard/daemon.c`, `clipboard/store.c`, `clipboard/store.h`, `clipboard/extension.c` | draft |
| snippets | `features/snippets.md` | `snippets/extension.c` | draft |
| setup-check | `features/setup-check.md` | `main.c` (`read_description`, `run_setup`) | draft |

## Coverage Notes

- Unmapped areas: 无。全部运行时文件已归入上述 8 个功能；`config.def.h/config.h/config.mk/Makefile/tests/*` 为 build code，见 PROJECT-MAP.md。
- Build-code entries (not features, see PROJECT-MAP.md): `Makefile`, `config.mk`, `config.def.h`, `config.h`, `tests/test_foundation.c`, `tests/test_snippets.c`, `tests/test_x11.c`, `tests/clipboard_x11.c`, `tests/fake_extension.c`, `tests/run_xvfb.sh`, `tests/run_snippets_xvfb.sh`。
- `main.c` 同时承载 cli-and-lifecycle 与 setup-check：后者是前者 setup 分支的子流程，独立成注因其只读语义与 GUI 互斥，值得单独 diff。
- `util.c` 同时服务 input-editing（UTF-8/trim）与 transport（nonblock/dup）：主归属记 input-editing，transport 侧引用。
- 自查：8 篇功能注的 Files & Roles 表与 PROJECT-MAP.md 树一致；`f654dde` 引入的 `SUPERCLIP_PROMPT`/`prompt › ` 已记入 x11-frontend 与 cli-and-lifecycle。
- 修复（2026-09-24，未提交）：4 blocking 全修——draw 钳位 256、ITEM 超限 `response_failed`+drain 到 END 报 `too many results`、`sc_child_quit()` 优雅关闭、overlong 3/4 字节拒收；major/minor 修——stale ERROR 纳入 id 过滤、snprintf 截断判定、每轮 stdout 上限 32、stderr 显示 `extension error`+首行、setup 控制字符转义。验证：`make check` 与 `make test-sanitize` 全过（sanitize 后需 `make clean && make` 重建正常二进制）。
- 新增测试：`test_protocol` 加 NUL/overlong 正反例；`--many-items`（256+10）验 drain 到 END；`--quit-echo` 验 QUIT 握手；另修 `read_one` 的 EPIPE 后 poll 旧 fd 忙等 bug 与 quit 测试的非阻塞读。

## Audit（fresh subagent，2026-09-24）

- Verdict: FAIL（4 blocking，见上）。审计确认笔记无 phantom 符号/文件/流程，
  禁用 API 检查通过（C 代码零 `system/popen/sh/dlopen/Wayland/GTK`；仅测试脚本
  与 make `$(shell)` 豁免命中）。
- 已据审计修复笔记：extension-transport §5/§6 的 draw 越界升 blocking 并给 fix
  方向；cli-and-lifecycle 新增“draw() 栈数组越界”小节。
- 审计新增 major（stale ERROR 无 id 检查、snprintf 截断误判、每轮无界 drain 饿死
  X11、扩展 TOCTOU）和 minor（stderr 未取首行、future/stale 不分、setup 输出未
  转义可伪造节、PROJECT-MAP 里 config.h/config.def.h“分歧”实为相同）已记录在此，
  待修代码时逐项处理。代码侧 4 blocking 尚未修（本次为只读理解+review，不改实现）。

## Review（按 skill Step 6 + review-checklists.md 自查）

- Correspondence：7 注引用的符号与行号均出自本次实际读取（`main.c:620/697/253/307/353/473/499/540`、`x11.c:26/65/212/310/349/378`、`text.c:9/22/36`、`protocol.c:80/110/140/201`、`extension.c:35/88/155/199/266/346/390/424`、`daemon.c:683`、`store.c:51/116/188`）——`x11.h` 行号引用的是结构字段而非行，属 minor 记述不精确，无 blocking。
- Security：读到的问题与 REQUIREMENTS 已知 gap 重合（`snprintf` 截断误判、`discover` 无界、`handle_child` 无界 drain、daemon 路径缺 DISPLAY 隔离、STRING 未转码），无新增 blocking；`system/popen/sh/dlopen` 零命中，符合不变量。
- Plausible correctness：入口均可达（`main`→各特性），错误路径均有 `status`/stderr 出口；`sc_x11_draw` 的 `prompt/query` 空指针容忍（`draw_text` 判 NULL）与 `cursor > len` 钳位已确认。
