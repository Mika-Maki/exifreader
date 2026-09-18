# 使用指南

[English](../en/usage.md) | **简体中文**

`exifreader` 读取一个或多个文件的 EXIF/TIFF 元数据并渲染输出。本页是命令行的完整
参考;[README](../../README.zh-CN.md) 里有简版。

## 用法

```
exifreader [options] <image> [image ...]
```

用单独一个 `-` 代替文件名,可从标准输入读取图像。不给任何文件时会打印用法并以退出码
`1` 结束。

## 输出模式

同一时刻只有一种模式生效,命令行中最后出现的那个优先。默认是信息树。

### 信息树(`-t`、`--tree`)

默认模式。嵌套目录用缩进表示,每个目录与每个取值都带有存储偏移与绝对文件偏移。

```sh
exifreader photo.jpg
```

### 每行一个标签(`--tags`)

扁平索引,每行一个标签,以竖线分隔:

```
IFD0 | Make (0x010F) | NIKON CORPORATION | ASCII[18]
```

四个字段依次是 `路径`、`名称 (id)`、`取值`、`类型[数量]`。该模式最便于用
`awk`、`cut`、`grep` 二次处理。

```sh
exifreader --tags photo.jpg | cut -d'|' -f2 | sort -u
```

### JSON(`-j`、`--json`)

结构化输出,每个取值同时给出格式化结果与原始表示,并附带偏移量。结构如下:

```json
{
  "source": "photo.jpg",
  "container": {
    "format": "JPEG",
    "detail": "JPEG",
    "width": 16,
    "height": 16,
    "exifPayloads": [{"offset": "0xC", "note": "APP1 Exif segment at 0x2"}]
  },
  "exif": {
    "found": true,
    "tiffOffset": "0xC",
    "endianness": "little",
    "bigTiff": false,
    "ifdCount": 5,
    "tagCount": 54,
    "makerNoteVendor": "Nikon",
    "warnings": [],
    "ifds": [
      {
        "ifd": "IFD0",
        "kind": "IFD0",
        "offset": "TIFF+0x8",
        "fileOffset": "0x14",
        "endianness": "little",
        "maker": "NIKON CORPORATION",
        "entryCount": 12,
        "nextIfd": "TIFF+0x9E",
        "tags": [
          {
            "id": "0x010F",
            "name": "Make",
            "type": "ASCII",
            "count": 18,
            "offset": "0x36E",
            "fileOffset": "0x37A",
            "inline": false,
            "value": "NIKON CORPORATION"
          }
        ],
        "children": []
      }
    ]
  }
}
```

解析前需要知道的几点:

* **嵌套目录在 `children` 里。** ExifIFD、GPS、Interop、SubIFDs 与 MakerNote 块,都是
  指向它们的那个 IFD 的子对象。顶层的 `ifds` 数组只包含 IFD0 与 IFD1 缩略图链。
* **偏移量是字符串**,以 `0x` 十六进制表示。`offset` 相对 TIFF 头,`fileOffset`
  是文件内的绝对位置。
* **多个输入时对象是拼接的**,不会包成数组 —— 每个文件输出一个格式化的对象。请对单个
  对象使用 `jq`,或改用 `-s` 只看摘要。
* **非有限浮点数是字符串。** `"nan"`、`"inf"`、`"-inf"` 以 JSON 字符串输出,因为
  裸 `nan`/`inf` 不是合法 JSON。因此看起来是数字的值,可能是数字,也可能是这几种字符串。
* **输出恒为合法 UTF-8。** 标签值中非法 UTF-8 字节会被替换为 U+FFFD。

```sh
exifreader --json photo.jpg | jq '.exif.ifds[0].tags[] | select(.name == "Make")'
exifreader --json photo.jpg | jq -r '.exif.warnings[]'
```

### 摘要(`-s`、`--summary`)

每张图一行:容器与尺寸、字节序、IFD 与标签数量,随后是人们通常最关心的若干标签。

```sh
exifreader --summary gallery/*.jpg
```

```
JPEG 16x16, little-endian, 5 IFD(s), 54 tag(s), Make=NIKON CORPORATION, Model=NIKON Z 8, \
DateTimeOriginal=2024:05:17 18:23:45, ExposureTime=0.008 (1/125), FNumber=2.8 (28/10), \
ISO=200, MakerNote=Nikon
```

## 内容选择

| 选项 | 作用 |
|------|------|
| `--no-values` | 只打印名称、id 与类型,不打印取值 |
| `--raw` | 额外以十六进制输出每个取值的原始字节 |
| `--no-makers` | 不进入 MakerNote 块 |
| `--no-thumbnails` | 不跟随 IFD1 缩略图目录 |
| `--no-warnings` | 在信息树与摘要中隐藏解析警告 |

`--no-values` 配合 `--tags`,是盘点一个文件带了哪些标签最快的方式:

```sh
exifreader --tags --no-values *.jpg | sort -u
```

## 资源上限

| 选项 | 默认值 | 含义 |
|------|--------|------|
| `--max-depth N` | 8 | IFD 最大嵌套深度;**夹紧到 256** |
| `--max-nodes N` | 200000 | 解析停止前可访问的目录预算 |

两者的存在,是为了让恶意文件无法让读取器递归到栈溢出,或无限分配。`--max-depth` 超过
256 会被下调并在 stderr 提示 —— 更大的上限只会换来栈溢出。

`--max-nodes` 统计的是目录数,不是标签数。预算耗尽时解析停止并记录一条警告,此时输出
中的标签数量反映的是实际访问到的部分。

## 提取缩略图

```sh
exifreader --extract out gallery/*.jpg
```

每个内嵌缩略图/预览图都会按原样写入 `out/`,以源文件名加序号后缀命名,并打印写入位置:

```
extracted thumbnail -> out/photo.jpg.thumb01.jpg (155 bytes)
```

字节不会被重新编码或校验 —— 工具只是写出文件所声称的内容。若输入不可信,提取结果同样
不可信。

## 查看内置字典

```sh
exifreader --list-tags                 # 默认(IFD0)字典
exifreader --list-tags exif            # ExifIFD
exifreader --list-tags gps             # GPS
exifreader --list-tags nikon           # 厂商 MakerNote 字典
```

已知种类:`ifd0`、`ifd1`、`exif`、`gps`、`interop`、`makernote` 以及厂商名
(Nikon、Canon、Apple、Fujifilm、Sony、Olympus、Panasonic、Pentax、Samsung、Leica、
Google、DJI)。每个列表给出标签 id、名称,以及解码器已知的取值含义。

## 退出码

| 退出码 | 含义 | 典型原因 |
|--------|------|----------|
| 0 | 读取成功,无异常 | |
| 1 | 用法错误,或文件无法读取 | 选项错误、文件不存在、权限不足 |
| 2 | 文件可读,但不含 EXIF/TIFF 数据 | PNG 无 `eXIf` 块、JPEG 无 APP1、非图像文件 |
| 3 | 找到 EXIF,但解析产生警告 | 条目被截断、成环、指针越界 |

退出码 `3` 是"成功但有保留",不是失败:有用的元数据仍在 stdout 上。想要"严格干净"的
脚本应显式判断是否为 `0`。

```sh
if exifreader --summary photo.jpg >/dev/null; then
    echo "clean read"
elif [ $? -eq 3 ]; then
    echo "read, but the file has anomalies"
fi
```

## 从管道读取

```sh
cat photo.jpg | exifreader -
curl -s https://example.invalid/photo.jpg | exifreader --summary -
```

无法确定长度的输入(FIFO、procfs 条目)会按流读取,而不会被当作空文件,因此 `-` 在
管道中可用。

## 常用配方

找出一批照片的拍摄时间:

```sh
exifreader --summary *.jpg | grep -o 'DateTimeOriginal=[^,]*'
```

按 GPS 列出在某个地点拍摄的照片:

```sh
for f in *.jpg; do
    lat=$(exifreader --json "$f" | jq -r '.exif.ifds[] | .children[]? | select(.kind=="GPS") | .tags[] | select(.name=="GPSLatitude") | .value')
    [ -n "$lat" ] && echo "$f $lat"
done
```

检查文件的元数据是否自洽:

```sh
exifreader --json photo.jpg | jq '{warnings: .exif.warnings, ifds: .exif.ifdCount}'
```

## 故障排查

**"no EXIF data found"(退出码 2)** —— 文件没有 EXIF/TIFF 载荷。PNG 需要 `eXIf` 块,
JPEG 需要 `APP1 Exif` 段。HEIC 支持取决于能否在 ISO-BMFF 元数据中定位 `Exif` 项。

**"IFD at 0x... already visited"** —— 文件把某个目录指向了已经用过的偏移。真实文件用它
表示共享目录,恶意文件用它制造环。读取器会停止下降并记录警告。

**偏移看起来不对** —— 记住有两个:相对 TIFF 头的 `offset` 与文件内真实位置的
`fileOffset`。与十六进制转储对照时请用 `fileOffset`。

**读取器提前结束** —— 多半是撞上了 `--max-nodes`。若信任该文件,可显式提高它。
