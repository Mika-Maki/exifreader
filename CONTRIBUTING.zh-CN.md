# 参与 exifreader 的开发

[English](CONTRIBUTING.md) | **简体中文**

感谢你愿意投入时间。本文说明如何构建项目、代码应当长什么样,以及如何让改动被合并。

## 社区准则

本项目遵循[贡献者公约](CODE_OF_CONDUCT.md)。参与即表示你遵守它。请将不可接受的行为
报告至 **conduct@example.com**。

## 贡献方式

* **缺陷报告** —— 使用 [Bug 表单](.github/ISSUE_TEMPLATE/bug_report.yml)。畸形文件才是
  重点:如果某个文件让读取器崩溃、卡死,或打印出不该打印的内容,那就是值得报告的缺陷。
* **功能需求** —— 开 [Feature 表单](.github/ISSUE_TEMPLATE/feature_request.yml),说明
  你需要的元数据容器或标签族。
* **Pull Request** —— 修复、补充标签字典、新增容器/解码支持、文档与测试都欢迎。

安全问题**不要**开公开 Issue —— 见 [SECURITY.zh-CN.md](SECURITY.zh-CN.md)。

## 快速开始

依赖:C++17 编译器(`g++` 或 `clang++`)、`make`,以及 Python 3(仅标准库,用于生成
测试样例)。CMake >= 3.16 可选。

```sh
git clone https://github.com/OWNER/exifreader.git
cd exifreader

make            # -> build/exifreader
make test       # 构建 + 端到端套件(43 项)
make debug      # ASan + UBSan 构建,并对它跑套件
```

CMake 路径等价:

```sh
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake -j
./tests/run_tests.sh build-cmake/exifreader
```

## 仓库结构

| 路径 | 内容 |
|------|------|
| `src/` | 读取器:容器嗅探、TIFF 解析、标签字典、渲染器、CLI |
| `tests/run_tests.sh` | 对各输出模式(含畸形输入)的端到端断言 |
| `tests/make_fixtures.py` | 生成具有已知磁盘字节布局的测试样例 |
| `docs/` | 英文与简体中文文档,策略见 [docs/i18n.md](docs/i18n.md) |
| `.github/workflows/` | CI、CodeQL 与发布流水线 |
| `scripts/` | 发布与上线脚本 |

`tests/fixtures/` 是生成物、不入库 —— 用 `make fixtures` 重建。

若你的内核无法启动 ASan(部分 Android/Termux 内核的用户空间小于 40 位),请改用
`make ubsan` 只开 UBSan。

## 代码风格

* C++17,无第三方依赖。解析器必须保持低分配,且面对畸形输入**不得抛异常**。
* 4 空格缩进、100 列上限、大括号紧随语句。风格定义见 [`.clang-format`](.clang-format):

  ```sh
  make format        # 就地格式化 src/
  make format-check  # 报告任何不符合 clang-format 的地方
  ```

  代码树早于该配置,因此 `make format-check` 仍会报告既有的差异,CI 中它是**咨询性**
  任务。请保证你改动的文件是干净的;顺手格式化无关代码的改动会被要求拆成单独 PR。
* 所有文件数据读取都必须经 `ByteReader`(带边界检查)或显式范围检查。绝不要用直接来自
  文件的偏移去索引缓冲区。
* 新增解析路径时,请同时保留存储偏移(TIFF 相对)与绝对文件偏移 —— 偏移量正是本工具
  的价值所在。
* 构建在 `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` 下零警告,CI 以 `-Werror`
  运行。除非你能说明该转换可证明在范围内,否则不要用强制转换消警告。
* 注释解释**为什么**,而不是**做了什么**。与周围语气保持一致:简短、务实、不加修饰。

## 测试

任何行为变更都需要样例与断言。请扩展 `tests/make_fixtures.py` 来添加样例(让字节在
仓库中可复现),并在 `tests/run_tests.sh` 中加断言。

```sh
make test                                  # 完整套件
./tests/run_tests.sh build/exifreader      # 针对特定二进制
```

CI 还会在 ASan+UBSan 下、以及每种编译器下各跑一次,只在本地通过的改动会被拦下。

## 提交信息

使用 Conventional Commits,便于生成变更日志:

```
<type>(<scope>): <subject>

<body>

<footer>
```

常用类型:`feat`、`fix`、`perf`、`refactor`、`docs`、`test`、`build`、`ci`、
`chore`。主题用祈使句、不超过 72 字符。用 `Fixes #123` / `Closes #123` 关联 Issue。

使用 AI 工具协助完成的提交,请加上 `Assisted-by:` 尾注(见
[AI_DISCLOSURE.zh-CN.md](AI_DISCLOSURE.zh-CN.md))。

## 文档与翻译(i18n)

* 英文文档是**规范版本**,中文文档是同步翻译。两者通过文件顶部的语言切换链接互指,由
  `scripts/check-i18n.sh` 在 CI 中强制校验。
* 改动英文文档时,请在同一个 PR 中更新对应译文;若暂时无法翻译,请把译文条目标记为
  待更新并在 PR 中说明。
* 新增一种语言:按 [docs/i18n.md](docs/i18n.md) 的约定添加 `<name>.<lang>.md`,注册到
  语言清单,并在语言切换行中加入该语言。
* 代码注释、标识符、CLI 长选项与机器可读输出保持英文,不翻译。

## Pull Request 流程

1. Fork 仓库,从 `main` 切分支。
2. 改动尽量小而聚焦;避免无关的重新格式化。
3. 本地运行 `make test` 与 `make format-check`。
4. 用户可见的变更请在 [CHANGELOG.md](CHANGELOG.md) 的 `## [Unreleased]` 下补充条目。
5. 提交 PR 并填写模板。CI 必须为绿。
6. 维护者评审;默认使用 squash 合并。

## 报告性能或健壮性问题

若读取器变慢,或某个文件让它行为异常,请附上:

* 完整命令行与退出码,
* 编译器与操作系统,
* 尽可能**最小化**的输入文件(截断或重新构造的样本最好;请先去掉隐私内容 —— EXIF
  通常含有 GPS 坐标与设备标识)。

## 许可证

提交贡献即表示你同意以 [MIT 许可证](LICENSE) 授权你的贡献。
