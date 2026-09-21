# 编写扩展

[English](EXTENSIONS.md)

扩展是一个可执行文件，通过 stdin 和 stdout 与 superclip 交换 v1 协议记录。扩展可以使用任何
语言编写。

## 安装扩展文件

把文件放在以下位置之一：

```text
$XDG_CONFIG_HOME/superclip/extensions/
$HOME/.config/superclip/extensions/    （未设置 XDG_CONFIG_HOME 时）
/usr/local/libexec/superclip/extensions/    （默认系统目录）
```

自定义构建时，系统目录位于编译进 superclip 的安装前缀下；`config.mk` 和 `config.h` 需要按
[README](../README_CN.md) 中的说明保持一致。

文件名就是扩展名和命令行子命令。名称以 ASCII 字母或数字开头，也可以包含 `.`、`_` 和 `-`；
`setup` 是保留名。文件需要有执行权限。

用户扩展与系统扩展同名时，优先使用用户扩展。

## 最小扩展

先创建用户扩展目录：

```sh
mkdir -p "${XDG_CONFIG_HOME:-$HOME/.config}/superclip/extensions"
```

把下面的例子保存为该目录下的 `hello`。保存后添加执行权限：

```sh
chmod 755 "${XDG_CONFIG_HOME:-$HOME/.config}/superclip/extensions/hello"
```

```python
#!/usr/bin/env python3
import sys

mode = sys.argv[1] if len(sys.argv) == 2 else ""

if mode == "--superclip-describe":
    print("SUPERCLIP\t1\thello\tshort\tenter\tHello")
elif mode == "--superclip-setup":
    print("NONE")
elif mode == "--superclip-query":
    kind, request_id, query = sys.stdin.readline().rstrip("\n").split("\t", 2)
    print(f"BEGIN\t{request_id}")
    print(f"ITEM\t{request_id}\thello\tSay hello\tA minimal result")
    print(f"END\t{request_id}")
elif mode == "--superclip-execute":
    kind, request_id, query, result_id = \
        sys.stdin.readline().rstrip("\n").split("\t", 3)
    if result_id == "hello":
        print(f"OK\t{request_id}")
    else:
        print(f"ERROR\t{request_id}\tUnknown result")
else:
    raise SystemExit(2)
```

先检查自描述，再打开扩展：

```sh
superclip setup
superclip hello
```

这个例子只返回一条固定结果。实际扩展应验证每条记录、解码转义字段、遵守协议上限，并把诊断
信息写到 stderr。

## 选择生命周期

| 进程模式 | 触发方式 | 用法 |
| --- | --- | --- |
| `short` | `enter` | 每次查询和动作各启动一个进程。 |
| `persistent` | `enter` | 面板打开期间复用一个进程；按 `Enter` 查询。 |
| `persistent` | `change` | 复用一个进程；输入停止变化后查询。 |

持久扩展实现 `--superclip-session`，并在收到 `QUIT` 或 EOF 后退出。前一个请求结束前可能收到新
请求，因此每条响应都要带上所属的 request ID。

## 返回设置建议

v1 协议允许 `--superclip-setup` 返回 `NONE`、`AUTOSTART` 和 `STATUS`，分别表示无需设置、
建议的启动命令和当前状态。当前前端接受 `NONE` 和 `AUTOSTART`，但仍会拒绝 `STATUS`；该缺口
记录在 [PROGRESS.md](../PROGRESS.md) 中。设置记录只提供信息，是否执行由用户决定。

记录格式、转义、上限和错误处理见[扩展协议 v1](PROTOCOL_CN.md)。完整规范见
[SPEC.md](../SPEC.md#9-扩展协议-v1)。
