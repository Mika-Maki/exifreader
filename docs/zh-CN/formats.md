# 格式与标签支持

[English](../en/formats.md) | **简体中文**

`exifreader` 目前能理解什么,以及同样重要的 —— 它**不能**理解什么。"识别"指容器扫描
发现了该载荷;"解码"指取值已被解析。

## 容器

| 容器 | 识别方式 | EXIF 载荷位置 | 备注 |
|------|----------|---------------|------|
| JPEG | 签名 `FF D8` | 带 `Exif\0\0` 头的 `APP1` 段 | 同时从 `SOFn` 报告尺寸与 JFIF 版本 |
| PNG | 签名 | `eXIf` 块 | |
| TIFF | `II*\0` / `MM\0*` / BigTIFF `43` | 文件本身即为 TIFF 结构 | 经典与 BigTIFF,两种字节序 |
| HEIF / HEIC / AVIF | ISO-BMFF `ftyp` | 通过 `iinf` 与 `iloc` 定位的 `Exif` 项 | 当项表不可用时会退化为扫描 TIFF 签名 |

不支持:WebP、JPEG 2000、JPEG XL、非 TIFF 基础的 RAW 格式(CR3 属 ISO-BMFF,可能部分
可用,走的是同一套 `Exif` 项路径),以及附属文件(`.xmp`、`.icc`)。

## 字段类型

所有 TIFF 字段类型都会解码,无论内联还是外置:

| 类型 | Id | 呈现形式 |
|------|----|----------|
| BYTE | 1 | 数字,配合 `--raw` 时为十六进制 |
| ASCII | 2 | 字符串 |
| SHORT | 3 | 数字 |
| LONG | 4 | 数字 |
| RATIONAL | 5 | `n/d` 加小数;曝光时间额外给出 `1/125` |
| SBYTE | 6 | 有符号数字 |
| UNDEFINED | 7 | 十六进制预览;已知标签则做专门解码 |
| SSHORT | 8 | 有符号数字 |
| SLONG | 9 | 有符号数字 |
| SRATIONAL | 10 | 有符号比值 |
| FLOAT | 11 | 数字(JSON 中为 `"nan"`/`"inf"`/`"-inf"`) |
| DOUBLE | 12 | 数字,规则同上 |
| IFD | 13 | 目录指针,4 字节宽 |
| LONG8 | 16 | BigTIFF 64 位数字 |
| SLONG8 | 17 | BigTIFF 64 位有符号数字 |
| IFD8 | 18 | BigTIFF 64 位目录指针 |

字节序取自 TIFF 头,并作用于每一次多字节读取,包括 BigTIFF 的 64 位字段。

## IFD 图

| 目录 | 来源 |
|------|------|
| IFD0 | TIFF 头中的首个 IFD |
| IFD1 | 从 IFD0 出发的 `next IFD` 链(通常是缩略图),最多 8 个目录 |
| ExifIFD | `ExifIFDPointer` 标签 |
| GPS IFD | `GPSIFDPointer` 标签 |
| Interop IFD | ExifIFD 内的 `InteroperabilityIFDPointer` 标签 |
| SubIFDs | `SubIFDs` 标签,最多 64 项 |
| MakerNote | `MakerNote` 标签,按厂商解析 |

每个目录都会报告存储偏移与绝对偏移、条目数量、字节序以及 next-IFD 指针。

## MakerNote 厂商

厂商由 `Make` 标签与该块自身的签名判定,已知厂商会套用其字典。

| 厂商 | 处理的布局 | 是否有字典 |
|------|------------|------------|
| Nikon | type 3:内嵌 TIFF 头,自带的字节序与 magic | 有 |
| Canon | 普通 IFD | 有 |
| Apple | 普通 IFD | 有 |
| Fujifilm | 带厂商头的目录指针 | 有 |
| Olympus | 目录指针 | 有 |
| Panasonic | 内嵌头 | 有 |
| Sony | 普通 IFD(多种变体) | 有 |
| Pentax、Samsung、Leica、Google、DJI | 普通 IFD | 有 |

未知厂商,以及已知厂商中的未知标签,都会照常显示:该块会报告大小与十六进制预览,而不是
被丢弃。加密或基于偏移表的深层厂商结构(Canon `CameraSettings`、Nikon `ShotInfo`)
以不透明数据呈现,不逐字段解码。

## 语义化解码

除原始取值外,解码器还知道以下含义:

* 曝光时间(输出为 `1/125`)、FNumber、光圈与 APEX 值,
* 方向、测光模式、闪光、白平衡、场景与曝光程序枚举,
* GPS 纬度/经度/海拔,包括六十进制三元组与参考半球,
* 带 ASCII/UNICODE/JIS 字符集前缀的 `UserComment`,
* 以 UTF-16 存储的 Windows `XP*` 标签,
* EXIF 版本字符串与 `ComponentsConfiguration`,
* `Copyright`、`Artist` 与日期时间族,含其各种格式变体。

## 未解码的内容

会被扫描发现但不解析:

* **XMP**(带 `http://ns.adobe.com/xap/1.0/` 头的 `APP1`)—— 它是 XML,需要另一套
  解析器。
* **IPTC/IIM**(带 `Photoshop 3.0` 头的 `APP13`)。
* **ICC 配置文件**(带 `ICC_PROFILE` 头的 `APP2`)。
* **JPEG 注释**与其他非 EXIF 段。

同样不在范围内:写入或修复元数据、重新编码图像、校验提取出的缩略图是否为合法图像,以及
对图像像素本身做任何解释。
