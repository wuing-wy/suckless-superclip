# Feature Note: snippets

> Status: `draft` — Date: 2026-09-24 — Code ref: `uncommitted`

## 1. Summary

用户自定义片段系统扩展：`persistent + change`，C 单文件实现；
纯文本片段文件实时过滤；选中后 fork 常驻 holder 直控 X11 `CLIPBOARD`，
面板退出后仍可粘贴；单实例替换 + 绝对路径 `--holder-stop` 显式退出。
文件：`snippets/extension.c`、`snippets/SPEC.md`、`snippets/README.md`；
构建/测试：`Makefile`、`tests/test_snippets.c`、`tests/run_snippets_xvfb.sh`。

## 2. Files & Roles

| File | Role in this feature | Key symbols |
|------|----------------------|-------------|
| `snippets/extension.c` | 片段加载/过滤/查询执行/holder 拥有/单实例/`--holder-stop` | `load_snippets`, `respond_query`, `respond_execute`, `holder_main`, `replace_holder`, `stop_pid`, `stop_holder_cmd`, `handle_stdin`, `main` |
| `snippets/SPEC.md` | 需求/接口/错误契约/测试计划 | REQ-1..REQ-7 |
| `snippets/README.md` | 文件格式/使用/`--holder-stop`/明文与 daemon 捕获警告 | — |
| `Makefile` | `snippets-extension` 窄链接 x11、安装/清理、测试编排 | `snippets-extension`, `test_snippets` |
| `tests/test_snippets.c` | 解析/过滤/ID/TOCTOU/describe-setup 单元测试 | `test_parse_filter`, `test_stale_title_recheck` |
| `tests/run_snippets_xvfb.sh` | Xvfb 拥有/替换/停止集成测试 | — |

## 3. How It Works

Entry point:

- 核心 `sc_child_spawn(path, "--superclip-session")` 起会话（`main.c:queue_query`
  persistent 分支）；`--superclip-query/execute` 单次模式兼容，`QUIT`/EOF 退出。

Control/data flow:

1. `load_snippets()` 每次请求重读文件（无缓存）：`ENOENT`=空集；`EACCES`/超 1MiB
   按请求回固定 `ERROR`；坏行跳过记数。界：raw 行 128KiB/标题 512B/正文 64KiB/
   扫描 1024 行/保留 256。
2. `respond_query()` 空查全列，非空对解码标题/正文大小写敏感子串过滤，文件序
   `ITEM<reqid><lineno:title><title><preview>`，预览首行 120B 码点截断，必发 `END`。
3. `respond_execute()` 拆不透明 `result-id`（首个 `:` 分行号/标题），重读比对标题
   防 TOCTOU；`stop_recorded()` 以文件内 `pid + starttime` 比对实时 starttime
   后替换旧 holder（不一致/解析失败只 `unlink` 不 kill）；`fork` 中间子（父立即
   `waitpid`） + 孙辈 `setsid`；`pipe` 1 字节 ready/fail ≤1s 握手 +
   `XGetSelectionOwner` 回查后才回 `OK`；pid 文件直接 `O_CREAT|O_EXCL` 原子写，
   并发先胜败者回 `cannot own clipboard`。
4. `holder_main()` 自有 Display 单线程 `poll(200ms/EINTR)+XNextEvent`（flag 唤醒，
   非 self-pipe；Xlib 只在主循环）：`TARGETS/TIMESTAMP近似值/
   UTF8/STRING` 单次服务，`INCR/MULTIPLE/SAVE_TARGETS` 以 `property=None` 拒绝；
   `SelectionClear`/SIGTERM 后 `unlink` pid 退出。
5. `--holder-stop` 读 pid 文件 `SIGTERM`+200ms+`SIGKILL`，幂等。

Exit / error paths:

- 未知行号/标题不符 → `unknown snippet`；拥有失败 → `cannot own clipboard`；
  stdin 协议错 → 会话失败 exit 1；holder 启动失败 → `ERROR` 且面板留错误。

## 4. Feature Connections

- Calls: `protocol-codec`（`sc_protocol_parse/serialize` 收发记录）；
  `input-editing` 的 `sc_utf8_valid`（标题/正文/预览边界校验）。
- Called by: `cli-and-lifecycle`（经 `extension-transport` 同第三方路径 spawn，
  无私有入口）；X11 粘贴方经 holder 的 `CLIPBOARD` 读取。
- Shared state / ordering: 片段文件（用户数据，明文）；pid 文件
  `$XDG_RUNTIME_DIR/superclip/snippets-holder.pid`（`DISPLAY` 不隔离，全局）；
  每次拥有触发 clipboard daemon best-effort 捕获（预期副作用）。

## 5. Notable Implementation

### 不透明复合 result-id 绕 v1 四字段限制

- Where: `snippets/extension.c: respond_query/respond_execute`, `snippets/SPEC.md: REQ-4/REQ-5`
- Why: v1 `EXECUTE` 定死 4 字段（`protocol.c:215 want=4`），核心 `queue_execute`
  只发 4 字段；把 `<行号>:<标题>` 塞进单 `result-id` 字段，核心原样回传零改动，
  扩展侧拆分重校验防 QUERY-EXECUTE 间改文件串片。
- What breaks if changed: 用裸行号则改文件即错贴；加第 5 字段则协议解析失败整会话挂。

### 握手后回 OK（比 clipboard 更强）

- Where: `respond_execute` pipe+poll ≤1s + `XGetSelectionOwner` 回查
- Why: fork 成功不等于拥有成功；先 `OK` 后拥有失败会关面板丢错误（SPEC §8.5 违例）。
- Detail: 中间子须父 `waitpid`（无僵尸）；握手写端跨 fork 保持，其余 pipe 端关闭。

### holder 单实例与干净退出

- Where: `replace_holder/stop_pid/write_pid_file/stop_holder_cmd/holder_main`
- Why: 面板退出后仍可粘贴必须有人拥有 selection；单实例防多 owner 打架；
  `unlink` + `--holder-stop` 幂等保证无残留（Definition of done 第 8 条）。
- Subtlety: PID 复用不用 starttime 单判，须叠加 owner/连接检查；运行时父目录
  允许创建（0700/UID），配置片段文件永不自动创建。

## 6. Doc-Claim Diff

| Doc claim (source) | Implementation | Match/Gap (info/warn/blocking) |
|---|---|---|
| persistent+change + 公开协议无私有入口 (`snippets/SPEC.md REQ-1/2`) | describe/setup/session/query/execute 实现 | Match |
| 片段文件格式/界/坏行跳过 (`snippets/SPEC.md REQ-3`) | `load_snippets` 实现；`test_parse_filter` 覆盖 | Match |
| 子串过滤/result-id/预览 (`snippets/SPEC.md REQ-4`) | `respond_query` 实现；单元测试覆盖 | Match |
| holder 拥有/握手/单实例/停止 (`snippets/SPEC.md REQ-5/6`) | `respond_execute/holder_main/replace_holder` 实现；Xvfb 覆盖 | Match |
| 构建安装无新增依赖 (`snippets/SPEC.md REQ-7`) | Makefile 窄链接 x11 | Match |
| 明文警告 + daemon 捕获副作用 (`snippets/README.md`) | README 明示；Xvfb 未断言历史增长 | Gap (info)：历史增长断言在 SPEC Test Plan，脚本未做 |
| `DISPLAY` 不隔离 (`snippets/SPEC.md`) | 全局 pid，与 clipboard 同类 gap，已文档化 | Gap (info，已明示) |
