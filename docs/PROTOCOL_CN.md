# 扩展协议 v1

[English](PROTOCOL.md)

本页描述 v1 要求的行为，规范定义以 [SPEC.md](../SPEC.md#9-扩展协议-v1) 为准。当前前端尚未
实现全部规则，包括 `STATUS` 处理和完整的 request ID 校验；进度见
[PROGRESS.md](../PROGRESS.md)。

## 编码

协议使用合法 UTF-8。每条记录以 LF 结尾，字段之间用 Tab 分隔，不允许 NUL。字段内的反斜杠、
Tab、LF 和 CR 分别编码为 `\\`、`\t`、`\n` 和 `\r`；其他反斜杠转义都是错误。

编码后的单条记录最多 65,536 字节，包括末尾 LF。一次查询最多包含 256 条 `ITEM`。实现可以设置
更低的本地上限。

stdout 只传输协议记录，诊断信息写到 stderr。

## 自描述与设置

superclip 调用 `extension --superclip-describe`，扩展只返回一条记录：

```text
SUPERCLIP<TAB>1<TAB>name<TAB>short|persistent<TAB>enter|change<TAB>display-name
```

`name` 必须与可执行文件名相同。有效组合是 `short + enter`、`persistent + enter` 和
`persistent + change`。

`extension --superclip-setup` 可以返回以下设置记录：

```text
NONE
AUTOSTART<TAB>label<TAB>command<TAB>description
STATUS<TAB>ok|warning|error<TAB>message
```

`command` 是显示给用户的文字。

## 短进程

每次查询时，superclip 启动 `extension --superclip-query`，写入一条记录，然后关闭 stdin：

```text
QUERY<TAB>request-id<TAB>query
```

扩展返回：

```text
BEGIN<TAB>request-id
ITEM<TAB>request-id<TAB>result-id<TAB>title<TAB>description
END<TAB>request-id
```

没有结果时也要返回 `BEGIN` 和 `END`。扩展也可以在 `BEGIN` 之前改为返回
`ERROR<TAB>request-id<TAB>message`。

执行动作时，superclip 启动 `extension --superclip-execute` 并写入：

```text
EXECUTE<TAB>request-id<TAB>query<TAB>result-id
```

返回以下一条：

```text
OK<TAB>request-id
ERROR<TAB>request-id<TAB>message
```

查询进程此时已经退出，所以动作请求会再次带上原查询。

## 持久进程

当前面板打开时，superclip 只启动一次 `extension --superclip-session`。它的 stdin 可以接收任意
数量的 `QUERY` 和 `EXECUTE`，最后收到：

```text
QUIT
```

响应格式与短进程相同，并且可以交错。每条响应都必须使用所属请求的 request ID。新查询发出后，
旧查询的响应仍会被读取，但不会显示。收到 `QUIT` 或 EOF 后退出。

## 错误处理

非法 UTF-8、错误转义、未知记录、字段数错误、意外的 request ID、记录超长、结果过多或缺少
结束记录，都会使当前扩展会话失败。格式正确的过期查询响应是例外：它会被读完并忽略。达到结果
数量上限后，superclip 仍会读到对应的 `END`，以保持数据流同步。

result ID 是原样交还扩展的不透明字符串。动作成功后面板关闭；动作失败时，错误会留在当前面板中。
