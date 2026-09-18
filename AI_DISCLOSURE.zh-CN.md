# AI 披露

[English](AI_DISCLOSURE.md) | **简体中文**

## 本项目由 AI 生成

**exifreader 是一个 AI 项目。** 本仓库中的源码、测试、测试样例、文档以及 GitHub 配置,
均由 AI 编码代理(大语言模型)生成,并由人类提供方向、评审与验收。

本文件的存在,是为了让任何阅读、使用、审计或引用本项目的人,在依赖它**之前**就知道它
是如何产生的。

## 范围

| 仓库部分 | 来源 |
|----------|------|
| `src/*.cpp`、`src/*.hpp` | AI 生成(LLM),经人工评审 |
| `tests/run_tests.sh`、`tests/make_fixtures.py` | AI 生成(LLM),经人工评审 |
| `README*.md`、`CHANGELOG.md`、`CONTRIBUTING*.md`、`SECURITY*.md`、本文件 | AI 生成(LLM),经人工评审 |
| `docs/`(英文与简体中文) | AI 生成(LLM),经人工评审 |
| `.github/`、`.clang-format`、`.clang-tidy`、构建文件 | AI 生成(LLM),经人工评审 |
| 加固审计与修复 | AI 生成的审计分析(LLM),修复由 AI 代理落地并用测试复验 |
| 最终是否公开发布 | 人类决定 |

未复用任何受版权保护的第三方源码。程序零依赖:只链接 C++ 标准库。标签字典、MakerNote
布局与容器格式,是对公开规范的实现(EXIF/TIFF、JFIF、PNG、ISO-BMFF)以及对真实文件的
观察结果。

## 这对你意味着什么

* **依赖前请先评审。** 生成的代码可能微妙地出错、可能误读规范,也可能看起来正确却在某个
  边界情况下处理错误。请把它当作来自一位陌生贡献者的代码。
* **测试也是生成的。** 套件通过只说明代码符合测试所断言的内容,不代表断言本身就是对的。
  测试样例是合成的、由同一个项目生成,不能替代真实文件语料。
* **偏移量与解码结果值得独立复核。** 如果你把 exifreader 用于取证或修复工作,请用第二个
  独立实现(例如 `exiftool`)确认关键取值。
* **本项目没有经过人类专家审计。** 它未经过独立的安全评审。见
  [SECURITY.zh-CN.md](SECURITY.zh-CN.md) 中描述的健壮性工作,来自自动化分析加上生成的
  修复;其中一部分可以从本仓库复现:CI 会跑 sanitizer 与一轮短程覆盖率引导模糊测试,载体
  本身也在仓库里(`fuzz/fuzz_exif.cc`,用 `make fuzz` 运行)。1.0.0 加固背后的长时间差分、
  暴力与 guard-page 测试是在仓库之外驱动的,**无法**从仓库复现。
* **许可。** 生成的内容以 [MIT 许可证](LICENSE)发布。与任何生成作品一样,其版权状态
  在不同司法辖区可能不同;许可证由本仓库的发布者授予。

## 本仓库使用的透明化约定

* 由 AI 协助产出的提交带有 `Assisted-by:` 尾注,注明模型/工具,形式参照 Linux 内核的
  [AI 编码助手](https://docs.kernel.org/process/ai-assistance.html)政策。
* 对本文件中披露内容的任何实质修改,都必须在 [CHANGELOG.md](CHANGELOG.md) 中说明。

## 对贡献者

贡献可以是 AI 生成的、人工编写的,或两者混合。无论哪种,[CONTRIBUTING.zh-CN.md](CONTRIBUTING.zh-CN.md)
的规则都不变:构建必须零警告、套件必须通过、新行为需要样例与断言,并且每个被合并的
Pull Request 都由一位人类负责。请在 PR 中说明是否使用了 AI 工具以及用于哪些部分。

## 语言

本披露以英文([AI_DISCLOSURE.md](AI_DISCLOSURE.md))为规范版本,简体中文为同步译文。
文档国际化策略见 [docs/i18n.md](docs/i18n.md)。
