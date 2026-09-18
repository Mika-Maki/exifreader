# exifreader - 图像 EXIF 信息树读取器

[English](README.md) | **简体中文**

[![CI](https://github.com/Mika-Maki/exifreader/actions/workflows/ci.yml/badge.svg)](https://github.com/Mika-Maki/exifreader/actions/workflows/ci.yml)
[![CodeQL](https://github.com/Mika-Maki/exifreader/actions/workflows/codeql.yml/badge.svg)](https://github.com/Mika-Maki/exifreader/actions/workflows/codeql.yml)
[![Release](https://github.com/Mika-Maki/exifreader/actions/workflows/release.yml/badge.svg)](https://github.com/Mika-Maki/exifreader/actions/workflows/release.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Dependencies: none](https://img.shields.io/badge/dependencies-none-brightgreen.svg)](#构建)
[![AI-generated](https://img.shields.io/badge/AI-generated-blueviolet.svg)](AI_DISCLOSURE.zh-CN.md)

一个零第三方依赖的 C++17 命令行工具,读取图像的 EXIF/TIFF 元数据,并以可导航的
**信息树**形式打印:容器 -> EXIF 载荷 -> IFD 目录 -> 标签 -> 解码后的值,并在每一层
标注偏移量。

它回答的不只是"ISO 是多少",而是每一段元数据**在哪里**(哪个段、哪个 IFD、哪个字节
偏移)——修图、取证时你需要的正是这个。

> [!IMPORTANT]
> **本项目由 AI 生成。** 源码、测试、测试样例与文档均由 AI 编码代理在人类指导下产出,
> 尚未经过人类专家的独立审计。依赖输出前请先阅读 [AI 披露说明](AI_DISCLOSURE.zh-CN.md),
> 关键偏移量与取值请用第二个实现交叉验证。

    $ exifreader photo.jpg
    photo.jpg
     +- container: JPEG - JPEG (JFIF 1.2)  320x240  [baseline SOF0]
     |   +- EXIF payload: APP1 Exif segment at 0x2
     +- TIFF header @ 0xC: II (little-endian), magic 42, IFD0 at TIFF+0x8 (file 0x14)
      |-- IFD0  @ TIFF+0x8 (file 0x14)  12 entries, little-endian, next IFD at TIFF+0x9E
          Make (0x010F)  : ASCII[18] = NIKON CORPORATION
          Orientation (0x0112)  : SHORT[1] inline = 1  [normal]
          XResolution (0x011A)  : RATIONAL[1] = 300
          ExifIFDPointer (0x8769)  : LONG[1] inline = 260 (0x104)
        |-- ExifIFD  @ TIFF+0x104 (file 0x110)  23 entries, little-endian
            ExposureTime (0x829A)  : RATIONAL[1] = 0.008 (1/125)
            FNumber (0x829D)  : RATIONAL[1] = 2.8 (28/10)
            Flash (0x9209)  : SHORT[1] inline = 16  [did not fire, suppressed]
          |-- MakerNote (Nikon type 3)  @ TIFF+0xA4 (file 0xB0)  3 entries, little-endian
              ISO (0x0002)  : SHORT[1] inline = 400 (0x190)
              Quality (0x0004)  : ASCII[14] = FINE
        |-- GPSIFD  @ TIFF+0x21E (file 0x22A)  9 entries, little-endian
            GPSLatitude (0x0002)  : RATIONAL[3] = 48 (48/1), 51 (51/1), 2934 (2934/100)
      |-- IFD1  @ TIFF+0x9E (file 0xAA)  8 entries, little-endian
          JPEGInterchangeFormat (0x0201)  : LONG[1] inline = 1238 (0x4D6)
     = 5 IFD(s) (incl. 1 thumbnail), 54 tag(s)

> 上例的标签名与枚举值按惯例保留英文,因为它们要与你正在排查的规范、厂商文档对齐;
> 说明性文字与文档为中文。详见[文档国际化策略](docs/i18n.md)。

## 特性

* **容器格式**:JPEG(APP1/Exif)、PNG(`eXIf` 块)、原始 TIFF(小端与大端、经典与
  BigTIFF)、HEIF/HEIC/AVIF(通过 ISO-BMFF 的 `iinf`/`iloc` 定位 `Exif` 项,并有
  签名扫描兜底)。
* **完整 IFD 图**:IFD0、IFD1 缩略图链、ExifIFD、GPS IFD、Interop IFD、`SubIFDs`
  与 MakerNote 块,每一项都同时给出**存储偏移**(相对 TIFF 头)与**绝对文件偏移**。
* **全部字段类型**解码:BYTE/SHORT/LONG/SLONG/RATIONAL/SRATIONAL/FLOAT/DOUBLE/
  ASCII/UNDEFINED,含内联(<= 4 字节)与外置取值。
* **语义化解码**:曝光时间输出为 `1/125`、光圈、APEX 值、方向/测光/闪光/白平衡/
  场景枚举、GPS 六十进制三元组、`UserComment` 字符集前缀(ASCII/UNICODE/JIS)、
  Windows `XP*` UTF-16 标签、EXIF 版本字符串、`ComponentsConfiguration`。
* **MakerNote 识别**:支持 Nikon、Canon、Apple、Fujifilm、Sony、Olympus、Panasonic、
  Pentax、Samsung、Leica、Google、DJI 的标签字典;Nikon type 3(内嵌 TIFF 头)、
  Olympus/Fujifilm(目录指针)与普通 IFD 块都能处理;无法识别的块也会显示大小与十六
  进制预览,而不是直接丢弃。
* **四种输出模式**:信息树(默认)、每行一个标签、保留原始值与偏移的结构化 JSON、
  单行摘要。
* **缩略图提取**(`--extract DIR`)。
* **恶意输入加固**:所有读取都对缓冲区做边界检查,目录做环检测,递归深度与目录数量
  有上限;异常的条目数量/越界载荷指针会作为警告报告,而不是崩溃。

## 构建

依赖:C++17 编译器(g++ 或 clang++)、make(或 CMake >= 3.16)。不使用任何第三方库。
`tests/make_fixtures.py`(仅用 Python 3 标准库)负责生成测试图片。

```sh
git clone https://github.com/Mika-Maki/exifreader.git
cd exifreader

make                 # -> build/exifreader
make test            # 构建 + 运行端到端套件

sudo make install    # -> $(PREFIX)/bin/exifreader,PREFIX 默认 /usr/local
```

或使用 CMake:

```sh
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --parallel
ctest --test-dir build-cmake --output-on-failure

cmake --install build-cmake --prefix /usr/local
```

常用选项与目标:

| 命令 | 作用 |
|------|------|
| `make debug` | 以 ASan + UBSan 重新构建 |
| `make fuzz` | 对解析器做覆盖率引导的模糊测试(需要 clang) |
| `make fixtures` | 重新生成 `tests/fixtures/` |
| `make format` / `make format-check` | 用 clang-format 处理 `src/`(见 CONTRIBUTING) |
| `make dist` | 在 `dist/` 生成 `HEAD` 的源码包与校验和 |
| `cmake -DEXIFREADER_WERROR=ON` | 把编译警告当作错误 |

带标签的发布版本会在[发布页](https://github.com/Mika-Maki/exifreader/releases)附带预编译
二进制与 `SHA256SUMS`。

## 用法

    exifreader [options] <image> [image ...]

    Output modes
      -t, --tree            indented information tree (default)
      --tags                one tag per line: ifd/path | name (id) | value | type
      -j, --json            structured JSON (raw + formatted values)
      -s, --summary         one compact line per image

    Content selection
      --no-values           omit values, print names/types only
      --raw                 also dump raw tag bytes (hex)
      --no-makers           do not parse MakerNote blocks
      --no-thumbnails       do not follow the IFD1 thumbnail directory
      --no-warnings         hide parser warnings
      --max-depth N         IFD recursion limit (default 8, clamped to 256)
      --max-nodes N         directory budget (default 200000)
      --extract DIR         write embedded thumbnails/previews into DIR

    Other
      --list-tags [KIND]    list a built-in dictionary (ifd0, ifd1, exif, gps,
                            interop, makernote or a vendor name)
      -h, --help            help
      -V, --version         version

命令行界面本身保持英文(便于脚本与国际化用户统一解析),单独一个 `-` 表示从 stdin
读取图像。

### 退出码

| 退出码 | 含义 |
|--------|------|
| 0 | 成功读取 EXIF,无异常 |
| 1 | 用法错误,或文件无法读取 |
| 2 | 文件可读,但解析不出 EXIF/TIFF 数据(输出为 `no EXIF data`) |
| 3 | 已解析出 EXIF,但文件存在异常(警告) |

退出码让它可以当作过滤器使用:`3` 表示元数据可疑,而不是"运行失败"。

### 示例

    exifreader photo.jpg                       # 信息树
    exifreader --tags --no-values *.jpg        # 所有标签的索引
    exifreader --json photo.heic | jq .exif.ifds[0].tags
    exifreader --extract out --summary gallery/*.jpg
    exifreader --list-tags exif                # 查看内置字典
    cat photo.jpg | exifreader -               # 直接从管道读取

## 目录结构

    src/exif_common.hpp       公共类型与小端/大端辅助
    src/byte_reader.hpp       对文件缓冲区的边界检查视图
    src/container.hpp/.cpp    容器嗅探(JPEG/PNG/TIFF/HEIF)
    src/tiff_parser.hpp/.cpp  IFD 树解析与取值解码
    src/tiff_tags.hpp/.cpp    标签字典(EXIF/GPS/Interop/MakerNote)
    src/renderer.hpp/.cpp     tree / tags / JSON / summary 渲染器
    src/main.cpp              命令行接口
    tests/make_fixtures.py    生成具有已知字节布局的测试图片
    tests/run_tests.sh        端到端测试套件
    fuzz/fuzz_exif.cc         解析器的 libFuzzer 测试载体
    scripts/                  发布与上线脚本
    docs/                     文档(英文与简体中文)
    .github/                  CI、CodeQL、发布工作流与模板

### 偏移量模型

每个目录与每个标签值都跟踪两个位置:文件里写下的**存储偏移**(相对 TIFF 头)与
**绝对文件偏移**。`readAt()` 把存储偏移映射为缓冲区下标,`absOf()` 把存储偏移映射
为文件偏移。自带 TIFF 头的厂商 MakerNote(Nikon type 3、Panasonic)会从一份自包含的
副本解析(`blobDelta_` 置位),因此其内部偏移可正确解析,而对外报告的偏移仍是文件
坐标。详见[架构说明](docs/zh-CN/architecture.md)。

## 测试

`tests/run_tests.sh` 本身不编译代码:它会生成测试样例,并断言各输出模式的可观察行为,
其中包括畸形输入。

    $ make test
    exifreader test suite
    ...
    passed: <n>  failed: 0

测试样例覆盖:带完整 IFD 图(Exif/GPS/Interop/SubIFD/缩略图)的 JPEG、缺少 Make 标签
的 JPEG、缺少 GPS/MakerNote 的 JPEG、PNG、原始 TIFF、大端 TIFF、HEIF、两种 MakerNote
布局(Nikon type 3 与普通 IFD 厂商块)、无元数据的 JPEG,以及截断、成环、异常计数、
越界指针、空文件与非图像文件。

CI 会用 g++ 与 clang++、经 Make 与 CMake 两条路径各跑一遍测试,并在 ASan + UBSan 下
再跑一遍。

ASan 需要内核提供足够大的用户态虚拟地址空间。在用户空间小于 40 位的内核上(部分
Android/Termux 内核),它会在启动时以 `heap size ... exceeds max user virtual address`
中止。此时请只用 UBSan:

```sh
make ubsan && ./tests/run_tests.sh build/exifreader     # 仅 UBSan,不含 ASan
make debug && ./tests/run_tests.sh build/exifreader     # ASan + UBSan
```

全部样例在两种构建下的所有模式均无告警。

`fuzz/fuzz_exif.cc` 是覆盖容器嗅探、TIFF 解析器、四种渲染器与缩略图发现的
libFuzzer 载体。CI 每次推送都会跑一轮短程模糊测试;本地可跑更长时间:

```sh
make fuzz CXX=clang++ FUZZ_SECONDS=600
```

## 健壮性说明

* `--max-depth` 被夹紧到 256:更大的上限只会让恶意的 SubIFD 链造成栈溢出,因此超过
  该值的请求会被下调(并在 stderr 给出提示)。
* BigTIFF 使用按字节序感知的 64 位读取;BigTIFF 文件中内嵌的经典条目(例如 Nikon
  type 3 MakerNote)保留自己的 2 字节计数与 12 字节条目;IFD 指针按其标签类型的宽度
  读取(LONG 在 BigTIFF 中仍是 4 字节)。
* 标签值在输出时被净化:tree/tags/summary 模式下 C0 控制字符与 DEL 打印为 `\xNN`,
  恶意载荷无法注入换行或 ANSI 转义;JSON 输出恒为合法 UTF-8,非法字节替换为 U+FFFD。
* 非有限 FLOAT/DOUBLE 值(NaN/Inf)在 JSON 中输出为字符串 `"nan"`、`"inf"`、
  `"-inf"`;裸 `nan`/`inf` 不是合法 JSON。
* 无法确定长度的输入(procfs 文件、FIFO)按流读取,而不是被当作空文件。
* 无符号偏移运算采用饱和语义;越界偏移/长度(包括负的 SLONG/SRATIONAL 缩略图值)会被
  拒绝,而不是回绕进缓冲区。

发布前的加固过程与规则汇总在 [CHANGELOG.md](CHANGELOG.md);由此得出的安全要求见
[SECURITY.zh-CN.md](SECURITY.zh-CN.md)。

## 已知限制

* MakerNote 字典刻意保持精简:未知标签只打印 id 与值、不给名称;深度嵌套的厂商结构
  (加密或基于偏移表的,如 Canon `CameraSettings`、Nikon `ShotInfo`)以不透明数据
  呈现,而不逐字段解码。
* `SubIFDs` 链每个标签最多 64 项,IFD1 兄弟链最多 8 个目录。
* 只读取 EXIF/TIFF 元数据:XMP、IPTC、ICC 载荷会在容器扫描中被发现,但不做解码。
* 缩略图提取按原样写出内嵌 JPEG/Strip 数据,不重新编码、不校验提取出的图像。

## 参与贡献

欢迎贡献。构建命令、代码风格与提交规范见 [CONTRIBUTING.zh-CN.md](CONTRIBUTING.zh-CN.md)
(英文版 [CONTRIBUTING.md](CONTRIBUTING.md)),社区准则见
[CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)。缺陷与需求请使用 GitHub 的 Issue 表单。

由于本项目由 AI 生成,贡献同样可以是 AI 生成的——请在 Pull Request 中说明,并且无论
如何都要对改动负责。

## 安全

exifreader 解析不受信任的文件。请按 [SECURITY.zh-CN.md](SECURITY.zh-CN.md) 私下报告
漏洞,**不要**开公开 Issue。

## 文档与国际化

* [文档索引](docs/README.md)
* [国际化(i18n)策略与翻译状态](docs/i18n.md)
* 中文指南:[使用](docs/zh-CN/usage.md)、[架构与偏移模型](docs/zh-CN/architecture.md)、
  [格式与标签支持](docs/zh-CN/formats.md)、[常见问题](docs/zh-CN/faq.md)

## AI 披露

本项目由 AI 生成。这意味着什么、对偏移量与解码值的正确性有何影响,以及本仓库使用的
透明化约定,见 [AI_DISCLOSURE.zh-CN.md](AI_DISCLOSURE.zh-CN.md)。

## 许可证

[MIT](LICENSE) (c) 2026 exifreader contributors。
