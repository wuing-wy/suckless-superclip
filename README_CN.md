# superclip

superclip 是一个用于 Linux/X11 的键盘命令面板。打开面板、选择扩展、输入查询，
然后执行扩展返回的动作。项目自带的 `clipboard` 扩展提供可搜索的剪贴板历史。

项目目前可用于开发，但尚未满足 `SPEC.md` 的全部验收要求。详见[当前进度](PROGRESS.md)。

[English](README.md)

## 编译

需要以下工具和库：

- C99 编译器、POSIX `make` 和 `pkg-config`
- Xlib、Xft、fontconfig 和 Xinerama
- [libgrapheme](https://libs.suckless.org/libgrapheme/)
- `superclip-clipboardd` 需要 XFixes
- 集成测试需要 Xvfb

在仓库根目录编译并测试：

```sh
make
make test
```

安装到 `/usr/local`：

```sh
sudo make install
```

如果要更换安装前缀，先运行 `make config.h`，再同时修改 `config.mk` 中的 `PREFIX` 和
`config.h` 中的 `SUPERCLIP_PREFIX`。两处必须一致，安装后的程序才能找到系统扩展。

## 启动

运行 `superclip` 选择扩展，也可以直接打开指定扩展：

```sh
superclip
superclip clipboard
```

请用 dwm、sxhkd 或常用的 X11 热键工具绑定这些命令。按键、配置和快捷键示例见
[使用手册](USAGE_CN.md)。

## 剪贴板历史

在 X11 会话中启动一次剪贴板 daemon：

```sh
superclip-clipboardd &
```

可以把这条命令加入 `.xinitrc` 或窗口管理器的 autostart 文件。查看状态或清空历史：

```sh
superclip-clipboardd status
superclip-clipboardd clear
```

历史以明文保存在 `$XDG_STATE_HOME/superclip/clipboard`；未设置
`XDG_STATE_HOME` 时，位置是 `~/.local/state/superclip/clipboard`。剪贴板中常有密码、
token 和其他隐私文本，请妥善保护或及时清理这个文件。

## 文档

- [使用手册](USAGE_CN.md)
- [编写扩展](docs/EXTENSIONS_CN.md)
- [扩展协议 v1](docs/PROTOCOL_CN.md)
- [设计哲学](docs/PHILOSOPHY_CN.md)
- [产品规格](SPEC.md)

中英文指南之间均有互链。
