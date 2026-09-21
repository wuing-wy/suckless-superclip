# 使用 superclip

[English](USAGE.md)

## 打开面板

打开扩展列表：

```sh
superclip
```

用 `Up`、`Down` 选择扩展，然后按 `Enter`。也可以用扩展的准确名称直接打开：

```sh
superclip clipboard
superclip clipboard 会议记录
```

扩展名之后的参数会用空格连接，并作为初始查询放入输入框。

## 按键

| 按键 | 操作 |
| --- | --- |
| `Enter` | 选择扩展、提交查询或执行当前结果 |
| `Up` / `Down` | 选择上一项或下一项 |
| `Ctrl-p` / `Ctrl-n` | 选择上一项或下一项 |
| `Left` / `Right` | 按一个 Unicode 字素簇移动光标 |
| `Home` / `End` | 移到查询开头或结尾 |
| `Backspace` / `Delete` | 删除一个 Unicode 字素簇 |
| `Ctrl-v` | 粘贴 X11 `CLIPBOARD` 中的文本 |
| `Escape` | 关闭面板 |

## 剪贴板历史

`clipboard` 扩展需要 `superclip-clipboardd`。每个 X11 会话启动一次：

```sh
superclip-clipboardd &
```

之后复制的文本会按新到旧出现在 `superclip clipboard` 中。输入文字可以过滤；选中一项并按
`Enter`，该文本会重新写入剪贴板。接近 256 KiB 上限的条目目前还不能可靠恢复，进度见
[PROGRESS.md](PROGRESS.md)。

原始内容不超过 256 KiB 时，daemon 会去掉首尾 Unicode 空白，并忽略裁剪后的空内容。原始内容
超过该上限时，目前会在裁剪前直接丢弃。剪贴板捕获是尽力而为；如果原应用过早退出或剪贴板
所有者切换太快，内容可能来不及保存。

历史以明文保存，可能含有密码、token 或其他隐私文本。文件位于
`$XDG_STATE_HOME/superclip/clipboard`；未设置 `XDG_STATE_HOME` 时位于
`~/.local/state/superclip/clipboard`。可以用 `superclip-clipboardd clear` 清空。

常用命令：

```sh
superclip setup
superclip-clipboardd status
superclip-clipboardd clear
```

`superclip setup` 只打印扩展给出的设置建议，不会代为执行。

## 快捷键

dwm 用户可以在它的 `config.h` 中加入：

```c
{ MODKEY,           XK_space, spawn, SHCMD("superclip") },
{ MODKEY|ShiftMask, XK_v,     spawn, SHCMD("superclip clipboard") },
```

sxhkd 用户可以在 `sxhkdrc` 中加入：

```text
super + space
    superclip

super + shift + v
    superclip clipboard
```

## 配置

如果 `config.h` 不存在，`make` 会从 `config.def.h` 创建它。修改 `config.h` 可以调整字体、
面板尺寸、可见行数、防抖时间，以及协议、查询和 stderr 上限。修改后重新编译安装：

```sh
make clean
make
sudo make install
```

更换安装前缀时，要让 `config.mk` 中的 `PREFIX` 与 `config.h` 中的
`SUPERCLIP_PREFIX` 保持一致。

剪贴板历史容量、后备字体缓存等上限目前还不能在这里配置，详见
[PROGRESS.md](PROGRESS.md)。

## 排查问题

- 面板无法打开：确认命令运行在 X11 会话中，并且设置了 `DISPLAY`。
- 剪贴板历史不可用：在同一 X11 会话中启动 `superclip-clipboardd`，再运行
  `superclip-clipboardd status`。
- 扩展没有出现：检查目录、文件名和执行权限，并用 `superclip setup` 检查自描述输出。

添加其他命令的方法见[编写扩展](docs/EXTENSIONS_CN.md)。
