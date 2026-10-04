# Smart Extract (智能解压缩) — 语言字符串

"Smart Extract" 功能在 7-Zip File Manager (7zFM.exe) 中新增了菜单项和提示消息。

7-Zip 的界面翻译不放在源码树里，而是通过语言文件 `Lang\<lang>.ttt` 单独分发
（安装目录的 `Lang` 文件夹）。因此：

- **英文字符串**已直接写入源码（`UI/FileManager/resource.rc` 的 STRINGTABLE），
  编译出的 7zFM.exe 无需语言文件即显示英文。
- **简体中文字符串**需要合并到已安装的 `zh-cn.ttt` 语言文件中，方法见下文。

## 新增的字符串 ID

| ID    | 用途                       | 英文默认值 |
|-------|----------------------------|------------|
| 560   | 7zFM File 菜单项           | `&Smart Extract` |
| 7220  | （保留）菜单/按钮文本      | `Smart Extract` |
| 7221  | 加密包回退提示消息（7zFM） | `Cannot list the contents of archive '{0}' (it can be encrypted). Smart extract uses the 'Extract to folder' mode for it.` |
| 2331  | 资源管理器右键菜单项（7-zip.dll）及 Tools > Options 里的条目 | `Smart Extract` |

## 合并到 zh-cn.ttt 的方法

1. 用支持 UTF-8 的文本编辑器打开 7-Zip 安装目录下的 `Lang\zh-cn.ttt`。
2. 找到菜单项行 `559=...`（"Alternate streams" 的翻译）。若不存在 559 行，
   则找到第一条 ID 大于 560 的行。在其后插入：

   ```
   560=智能解压缩
   ```

3. 找到 `2330=...`（"Compress to {0} and email" 的翻译）。在其后插入：

   ```
   2331=智能解压缩
   ```

4. 找到 `7206=...`（"Info" 按钮的翻译）。在其后插入以下两行：

   ```
   7220=智能解压缩
   7221=无法列出压缩包内容 "{0}"（可能已加密）。\n智能解压缩对它使用"解压到文件夹"模式。
   ```

5. 保存（保持 UTF-8 编码），重启 7-Zip File Manager 和资源管理器，
   并把语言切换为简体中文（Tools > Options > Language）。

## 注意

- ttt 文件中的 ID 必须保持递增；插入位置错误会导致整个语言文件加载失败
  （7-Zip 会回退到英文界面）。插入前建议备份原文件。
- `\n` 是 ttt 格式支持的换行转义。
- `0=7-Zip` 是语言文件的标识行，不要改动。
