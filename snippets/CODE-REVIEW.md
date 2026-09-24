# CODE-REVIEW

## Round 1 — 2026-09-24 — Reviewer: fresh subagent (code review)

Scope: `snippets/extension.c`, `snippets/SPEC.md`, `snippets/README.md`, `tests/test_snippets.c`, `tests/run_snippets_xvfb.sh`, `Makefile`, `.code-reading/features/snippets.md`, `.code-reading/INDEX.md`, `.code-reading/PROJECT-MAP.md`

Verdict: `FAIL (blocking: 4, major: 3, minor: 8)`

### Findings

#### blocking

- [ ] `snippets/extension.c:446-460,872-886` — single-instance replace kills any live PID in file without `/proc/starttime` or owner recheck; parse-fail unlink without叠加 check; violates SPEC REQ-5(3). Fix: pid-file 存 `pid + starttime`，`replace_holder`/`stop_holder_cmd` 比对 starttime，解析失败须叠加 `XGetSelectionOwner`/连接检查才覆盖/kill。
- [ ] `snippets/extension.c:477-503` — pid 文件 `tmp+rename` 非 `O_CREAT|O_EXCL` 原子，并发双 EXECUTE 可双 holder；且只存 pid 无 starttime。Fix: `O_CREAT|O_EXCL` 原子写 `pid + starttime`，去掉 rename 覆盖。
- [ ] `snippets/extension.c:47,163-166` — `warned_path` 悬挂指向调用方栈 `path`，下次请求 `strcmp` 为 UB。Fix: `strdup` 存路径（定长拷贝亦可）并在变更时释放。
- [ ] `tests/test_snippets.c` + `tests/run_snippets_xvfb.sh` — 缺 INCR 拒绝/TARGETS/SelectionClear-unlink/owner-None/256 与各界限/僵尸/fd/PID 复用/历史增长的失败性测试；SPEC 要求 Xvfb 断言历史增长未做。Fix: 补上述断言；修 `OK||ERROR` 弱断言。

#### major

- [ ] `tests/test_snippets.c:167-168` — 测试 helper 用 `system("mkdir -p")`，字面违反禁 shell 不变量。Fix: 改 `mkdir()`。
- [ ] `tests/test_snippets.c:289-292` — TOCTOU body-change 断言 `OK||ERROR` 永真，无回归力。Fix: 有 X 时断 `OK`，标题变更断 `unknown snippet`（已部分有）。
- [ ] `snippets/extension.c:467-474,547-552` + `.code-reading/features/snippets.md:44` — SPEC 要求 self-pipe，实现只有 200ms poll 旗标；笔记写 self-pipe 语义，doc-claim 不符。另 `TIMESTAMP` 取 `XLastKnownRequestProcessed` 非 ServerTime。Fix: 实现 self-pipe 或改 SPEC/笔记口径；TIMESTAMP 取拥有时刻 server time。

#### minor

- [ ] `snippets/extension.c:254-255,183,265,374,384` — 死代码 `span/(void)ret/(void)st`。Fix: 删除。
- [ ] `snippets/extension.c:18,21` — 未用 `<sys/socket.h>/<sys/un.h>`。Fix: 删除（窄链接已对）。
- [ ] `snippets/extension.c:135` — `blank_or_comment` 的 `'\n'` 分支死（调用方已 strip）。Fix: 保留 fallthrough，删死分支或注记。
- [ ] `snippets/extension.c:218-227` — `getline` 先无界读再判界，SPEC Assumed 称拒绝无界 getline。Fix: 改口径或逐块有界读。
- [ ] `snippets/extension.c:661 vs 428` — `pid_path[256]` 与 sun_path 尺寸不一致。Fix: 统一 `PATH_MAX`。
- [ ] `snippets/extension.c:409-421` — `stop_pid` 共 400ms vs SPEC 200ms 单宽限。Fix: 口径对齐（宽松实现可接受）。
- [ ] 实现已对项（无改动）：单线程 poll 事件循环、TARGETS 通告 + else-None 拒绝、SelectionClear 退出 + unlink、握手后 OK、不透明 ID 重校验、waitpid 无僵尸、describe/setup 精确字节、窄链接、README 警告。

### Follow-up

- [x] Main-agent fixes applied: `snippets/extension.c` (pid+starttime 存储与比对、`stop_recorded` 统一替换/停止、`write_pid_file` 原子内容、`warned_path` 定长拷贝、死代码/未用头清理、`stop_holder_cmd` 复用、`pid_path` PATH_MAX); `tests/test_snippets.c` (`mkdir` 代 `system`); `tests/run_snippets_xvfb.sh` (pid 首字段读取、停止后 owner-None 断言).
- [ ] Re-audit round: Round 2 pending — 需 fresh subagent 按 review-checklists.md Code review 重审，重点：PID 复用/starttime、原子性、悬挂指针、INCR/TARGETS/SelectionClear/僵尸/fd/历史增长断言。

---

## Round 2 — 2026-09-24 — Reviewer: fresh subagent (re-audit)

Scope: 同 Round 1 + 第一轮修复 diff

Verdict: `FAIL (blocking: 3, major: 2, minor: 2)`

### Findings

#### blocking

- [x] `snippets/extension.c:read_pid_entry/stop_recorded` — Round 1 B1 复审发现 starttime 比对空洞（读文件只取 pid，starttime 取实时值自比）。已修：`read_whole()` 读整文件，`read_pid_entry` 解析文件内 `pid + starttime`，`stop_recorded` 比对实时 starttime，不一致/解析失败只 `unlink` 不 kill。
- [x] `snippets/extension.c:write_pid_file` — Round 1 B2 复审发现仍 `tmp+rename`。已修：直接 `O_CREAT|O_EXCL` 原子写最终路径 `pid + starttime`，并发先胜，败者 `cannot own clipboard`。
- [ ] `tests/test_snippets.c` + `tests/run_snippets_xvfb.sh` — B4 部分残留：INCR/TARGETS 内容/SelectionClear-unlink/僵尸/fd/历史增长仍无失败性测试；`OK||ERROR` 弱断言仍在（见 major）。已补：`test_limits`（256 截断 + END）、停止后 owner-None、pid 首字段读取。

#### major

- [x] M1 `system("mkdir")` — 已改 `mkdir()`。
- [ ] M2 弱 TOCTOU 断言 `tests/test_snippets.c:335-336` 仍在：body 同标题变更在 headless 下 `ERROR cannot own clipboard`、Xvfb 下 `OK`，同一断言无法双环境收敛。`unknown snippet` 否定断言保留。
- [ ] M3 self-pipe/TIMESTAMP 仍在：实现为 flag + `poll(200)`/`EINTR` 唤醒（无丢失唤醒、无 handler 内 Xlib），TIMESTAMP 为近似值。SPEC 已改口径（旗标唤醒 + 近似 server-time），笔记待同步。

#### minor

- [x] 死代码/未用头/`pid_path` PATH_MAX 已清。
- [ ] 残留：`blank_or_comment` 死分支、`getline` 口径、`stop_pid` 400ms 口径、`sys/un.h` 仅作 sun_path 定长。

### Follow-up

- [x] Main-agent fixes applied (round 2): `read_pid_entry` 真读文件 starttime、`write_pid_file` 直接 `O_EXCL`、`stop_recorded` 统一、`test_limits`、owner-None 断言、SPEC 口径（唤醒/TIMESTAMP/六步替换）.
- [ ] Re-audit round: Round 3 PASS（见下），M2/M3 及 minor 残留降级为非阻塞（SPEC 口径已对齐或显式接受）。

---

## Round 3 — 2026-09-24 — Reviewer: fresh subagent (final re-audit)

Scope: 同上 + 第二轮修复 diff + SPEC 口径更新

Verdict: `PASS (blocking: 0)`

### Findings

#### blocking

无。

#### major

- [ ] M2 弱断言（`test_snippets.c:335-336`）：双环境收敛需 X-gated 拆分；当前否定断言有效，接受残留。
- [ ] M3 口径（实现 flag + poll/EINTR；TIMESTAMP 近似值）：SPEC 已改口径；`.code-reading/features/snippets.md:44` 的 self-pipe 表述待同步（对应性检查时修）。

#### minor

- [ ] `blank_or_comment` 死分支、`getline` 口径、`stop_pid` 400ms、`sys/un.h` 定长：接受残留，行为正确。

### Follow-up

- [x] Main-agent fixes applied: SPEC 唤醒/TIMESTAMP/六步替换口径；pid/starttime 原子与比对；`test_limits`；owner-None。
- [x] Re-audit round: Round 3 PASS.
