# exifreader documentation

**English** | [简体中文](zh-CN/README.md)

This directory holds the long-form documentation. The [README](../README.md) is
the front page; everything below goes deeper.

## Guides

| Guide | English | 简体中文 |
|-------|---------|--------|
| Using the command line: every mode, option, exit code and recipe | [usage](en/usage.md) | [使用指南](zh-CN/usage.md) |
| How the reader works: pipeline, module map, offset model, invariants | [architecture](en/architecture.md) | [架构说明](zh-CN/architecture.md) |
| What is supported: containers, field types, vendor MakerNotes, gaps | [formats](en/formats.md) | [格式支持](zh-CN/formats.md) |
| Questions people actually ask, and how to debug a bad read | [faq](en/faq.md) | [常见问题](zh-CN/faq.md) |
| How documentation is translated and kept in sync | [i18n](i18n.md) | [国际化策略](zh-CN/i18n.md) |

## Project documents

| Document | English | 简体中文 |
|----------|---------|--------|
| Overview and quick start | [README](../README.md) | [README](../README.zh-CN.md) |
| How to contribute | [CONTRIBUTING](../CONTRIBUTING.md) | [参与贡献](../CONTRIBUTING.zh-CN.md) |
| Security policy and threat model | [SECURITY](../SECURITY.md) | [安全策略](../SECURITY.zh-CN.md) |
| AI disclosure | [AI_DISCLOSURE](../AI_DISCLOSURE.md) | [AI 披露](../AI_DISCLOSURE.zh-CN.md) |
| Release history | [CHANGELOG](../CHANGELOG.md) | English only (see [i18n](i18n.md)) |
| Community expectations | [CODE_OF_CONDUCT](../CODE_OF_CONDUCT.md) | English only (upstream text) |

## Documentation conventions

* English is the canonical language. Translations are expected to track it; the
  contract is described in [i18n.md](i18n.md).
* Every translated page carries a language switcher on its first line, and
  `scripts/check-i18n.sh` enforces that in CI.
* Concrete facts, offsets and limits in these pages are checked against the
  source. When you change behaviour, change the page in the same pull request.
