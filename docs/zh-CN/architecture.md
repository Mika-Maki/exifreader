# 架构说明

[English](../en/architecture.md) | **简体中文**

本页说明一个文件是如何变成信息树的,以及代码**不得破坏**哪些不变量。面向准备改动解析器
的人。

## 流水线

```
file bytes
   |
   v
容器嗅探              src/container.cpp      -> ContainerInfo、EmbeddedExif[]
   |                                            (JPEG / PNG / TIFF / ISO-BMFF)
   v
TIFF 头               src/tiff_parser.cpp    -> TiffHeaderInfo
   |                                            (字节序、magic、首个 IFD、
   |                                             经典 vs BigTIFF)
   v
IFD 图遍历            src/tiff_parser.cpp    -> IfdNode 树 + TagEntry 列表
   |                                            (IFD0、IFD1 链、ExifIFD、
   |                                             GPS、Interop、SubIFDs、
   |                                             MakerNote blob)
   v
取值解码              src/tiff_parser.cpp    -> 格式化值 + 原始值
   |                  src/tiff_tags.cpp        (名称查找、枚举、GPS……)
   v
渲染                  src/renderer.cpp       -> tree / tags / JSON / summary
```

`main.cpp` 负责命令行:解析选项、读取输入(整文件,或从管道流式读取)、调用解析器、
调用渲染器,并据结果计算退出码。

## 模块划分

| 模块 | 职责 |
|------|------|
| `exif_common.hpp` | 公共类型(`Endian`)、字节交换辅助、字符串工具 |
| `byte_reader.hpp` | `ByteReader`:对文件缓冲区的受检视图;每个访问器都做边界检查,通过 `bool` 返回值报告失败而不抛异常 |
| `container.*` | 格式嗅探;产出 `ContainerInfo`(格式、细节、尺寸)以及带偏移的 `EmbeddedExif` 列表 |
| `tiff_parser.*` | `TiffHeaderInfo`、`TagEntry`、`IfdNode`、`ParseOptions`、`ParseResult`,以及遍历 IFD 图、解码取值的 `Parser` |
| `tiff_tags.*` | 标签字典:IFD0/IFD1/Exif/GPS/Interop 与各厂商 MakerNote 的 `TagInfo` 表,以及枚举/格式化回调 |
| `renderer.*` | 四种渲染器、输出转义,以及供 `--extract` 使用的 `findThumbnails()` |

解析器面对畸形输入**绝不抛异常**,除返回的字符串外不为单个标签分配内存。失败通过
`ParseResult::warnings` 与可选字段缺省来体现,而不是异常。

## 双坐标偏移模型

每个目录、每个取值都有两个位置,把它们区分开正是本工具的意义所在:

* **存储偏移** —— 文件里实际写下的值,相对 TIFF 头(或相对 MakerNote blob 自己的头);
* **绝对文件偏移** —— 字节在文件中真正的位置。

`Parser` 上有三个辅助函数负责坐标换算:

| 辅助函数 | 方向 | 用途 |
|----------|------|------|
| `readAt(stored)` | 存储偏移 -> 缓冲区下标 | 从哪里读字节 |
| `absOf(stored)` | 存储偏移 -> 文件偏移 | 打印什么 |
| `fileOf(bufferIndex)` | 缓冲区下标 -> 文件偏移 | 处理外置取值 |

它们由三个状态变量驱动:

```
tiffDelta_      顶层文件的 TIFF 头的缓冲区下标
blobDelta_      自包含 blob 的 TIFF 头的缓冲区下标
blobFileBase_   该 blob 起始处的文件偏移
inBlob_         是否正在读取这样的 blob
```

在顶层,`fileOf()` 是恒等映射:缓冲区**就是**文件。在 blob 内部,映射会减去
`blobDelta_`、加上 `blobFileBase_`,因此内部偏移可正确解析,而对外的偏移仍是文件
坐标。

### 自包含的 MakerNote blob

部分厂商会在 MakerNote 取值里再嵌入一套完整的 TIFF 结构(Nikon type 3、Panasonic)。
解析器把该区域复制成自包含缓冲区,让 `blobDelta_` 指向其头,并在 `inBlob_` 置位的情况下
解析。`parseEmbedded()` 前后的保存/恢复覆盖了 `tiffDelta_`、`blobDelta_`、
`blobFileBase_`、`inBlob_` 以及 BigTIFF/条目宽度状态,因此 BigTIFF 文件里的经典条目
blob 会按其自身的 12 字节条目布局解析。

## 安全不变量

以下规则保证恶意文件不会变成崩溃。削弱其中任何一条都属安全回归。

1. **不存在未检查的索引。** 读取要么经 `ByteReader`(短读返回 `false`),要么有显式
   范围检查。直接来自文件的偏移在使用前必须校验。
2. **饱和运算。** 偏移运算使用 `addClamped()` / `subClamped()`,使 `偏移 + 长度`
   不会回绕。负值或荒谬值(`SLONG`/`SRATIONAL` 缩略图偏移)一律拒绝,绝不回绕。
3. **条目数量在使用前校验。** 数量与文件剩余空间按**除法**比较,而不是乘以条目大小
   (那会回绕);并且每个目录另有 `kMaxEntriesPerIfd`(2^20)上限。
4. **检测环,而不只是扛住。** 每个已访问的目录偏移都会被记住;重复出现会报告
   "already visited (cycle or shared pointer)" 且不再下降。真实文件会合法地共享目录。
5. **深度有上限。** `kMaxIfdDepth` 为 256,CLI 与库内都把 `--max-depth` 夹紧到它 ——
   更大的上限只是给恶意的 `SubIFDs` 链准备一次栈溢出。
6. **工作量有预算。** `--max-nodes` 限制访问的目录数,因此"昂贵但有限"的文件也会终止。
7. **输出无法被伪造。** `escapeForDisplay()` 把 C0 控制字符与 DEL 变成文本模式下的
   `\xNN`;`escapeJson()` 保证合法 UTF-8(非法字节变 U+FFFD),非有限浮点变成字符串
   `"nan"`/`"inf"`/`"-inf"`,因此 JSON 永远可解析。

`typeSize()` 与读取路径必须对每种字段类型的宽度保持一致:IFD 类型(13)在两边都是
4 字节 —— 这正是发布前审计发现的那一类缺陷。

## 取值解码

对每个条目,解析器判断取值是内联存放(能放进 4 字节值域,BigTIFF 为 8 字节)还是外置,
再按正确宽度与数量读取,然后交给标签表。表项决定人类可读形式:普通字符串、数字、渲染为
`1/125` 的有理数、枚举标签、GPS 六十进制三元组、版本字符串等等。

若某个标签没有表项,仍会报告其 id、宽度、数量与原始值 —— 未知标签是数据,不是错误。

## 如何扩展

**新增标签** —— 在 `tiff_tags.cpp` 的相应表(或厂商 MakerNote 表)中加一项,给出 id、
名称,必要时给格式化函数;然后在 `tests/run_tests.sh` 加断言,若需要新的字节布局,再给
`tests/make_fixtures.py` 加样例。

**新增容器** —— 让 `container.cpp` 识别签名并产出 `ContainerInfo` 及所有 EXIF 载荷的
偏移。下游与容器无关:它只需要知道 TIFF 头在哪。容器支持是否合格的判据是:具有已知布局的
样例能解析出已知偏移。

**新增输出模式** —— 在 `renderer.cpp` 加渲染器,在 `main.cpp` 接线。请保持 JSON schema
稳定:脚本依赖这些键名,机器可读模式不做本地化。
