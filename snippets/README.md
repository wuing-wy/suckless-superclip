# snippets 扩展

`snippets` 是 `persistent + change` 的系统扩展：从纯文本文件读自定义片段，
输入即过滤，选中后由常驻 holder 拥有 X11 `CLIPBOARD`，面板退出后仍可粘贴。

[协议](../docs/PROTOCOL.md) · [写扩展](../docs/EXTENSIONS.md) · [SPEC](SPEC.md)

## 片段文件

路径：`$XDG_CONFIG_HOME/superclip/snippets`（`XDG_CONFIG_HOME` 为空视作未设，
用 `$HOME/.config/superclip/snippets`）。缺文件视为空集，不报错。

格式：每行 `标题<TAB>正文`，正文转义 `\\ \t \n \r`（与协议同构）。
空行、全空白行、`#` 开头行跳过；坏行（无 `TAB`、转义非法、标题空/超 512B、
正文超 64KiB、raw 行超 128KiB）跳过并记 `snippets: skipped N bad lines`。

```text
# title<TAB>body
deploy	kubectl rollout restart deploy/api\nkubectl rollout status deploy/api
daily	Today I did: 
```

界：文件 ≤1MiB、扫描 ≤1024 行、保留文件序前 256 个有效片段；预览取正文首行
截断 120B。

## 使用

```sh
mkdir -p "${XDG_CONFIG_HOME:-$HOME/.config}/superclip"
# 编辑 $XDG_CONFIG_HOME/superclip/snippets（上例格式）
superclip snippets
```

输入过滤、选中 Enter 即拥有到 `CLIPBOARD`，面板关闭后粘贴。显式停 holder：

```sh
/usr/local/libexec/superclip/extensions/snippets --holder-stop
```

## 警告

- 片段文件为明文，可能含密码/token，勿存敏感信息；组/他可读会警告。
- 每次拥有会被 clipboard daemon best-effort 捕获追加进剪贴板历史。
- holder 全局单实例（不按 `DISPLAY` 隔离），新执行替换旧 holder。
