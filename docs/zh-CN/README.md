# exifreader 文档

[English](../README.md) | **简体中文**

本目录存放长文档。[README](../../README.zh-CN.md) 是首页;以下内容更深入。

## 指南

| 指南 | English | 简体中文 |
|------|---------|--------|
| 命令行使用:全部模式、选项、退出码与配方 | [usage](../en/usage.md) | [使用指南](../zh-CN/usage.md) |
| 工作原理:流水线、模块划分、偏移模型、不变量 | [architecture](../en/architecture.md) | [架构说明](../zh-CN/architecture.md) |
| 支持范围:容器、字段类型、厂商 MakerNote、缺口 | [formats](../en/formats.md) | [格式支持](../zh-CN/formats.md) |
| 人们真正会问的问题,以及如何排查一次坏读 | [faq](../en/faq.md) | [常见问题](../zh-CN/faq.md) |
| 文档如何翻译与保持同步 | [i18n](../i18n.md) | [国际化策略](i18n.md) |

## 项目文档

| 文档 | English | 简体中文 |
|------|---------|--------|
| 概览与快速开始 | [README](../../README.md) | [README](../../README.zh-CN.md) |
| 如何贡献 | [CONTRIBUTING](../../CONTRIBUTING.md) | [参与贡献](../../CONTRIBUTING.zh-CN.md) |
| 安全策略与威胁模型 | [SECURITY](../../SECURITY.md) | [安全策略](../../SECURITY.zh-CN.md) |
| AI 披露 | [AI_DISCLOSURE](../../AI_DISCLOSURE.md) | [AI 披露](../../AI_DISCLOSURE.zh-CN.md) |
| 发布历史 | [CHANGELOG](../../CHANGELOG.md) | 仅英文(见 [i18n](i18n.md)) |
| 社区准则 | [CODE_OF_CONDUCT](../../CODE_OF_CONDUCT.md) | 仅英文(上游文本) |

## 文档约定

* 英文是规范语言。译文应当跟随英文;契约见 [i18n 策略](i18n.md)。
* 每个译文页面的首行都有语言切换行,`scripts/check-i18n.sh` 会在 CI 中强制校验。
* 本目录中的具体事实、偏移与上限都与源码核对过。改动行为时,请在同一个 Pull Request 中
  更新对应页面及译文。
