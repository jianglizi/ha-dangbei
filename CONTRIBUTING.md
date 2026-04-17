# Contributing

感谢你为 `ha-dangbei` 提交改进。

这个仓库保持轻量，不引入完整测试体系，但提交前请至少完成下面几项基础检查：

1. 变更范围尽量单一，避免把功能修改、重构和文档调整混在一个 PR 中。
2. Python 代码应保持 Home Assistant 自定义集成的常见风格，优先可读性和稳定性。
3. 如有行为变化，请同步更新 `README.md` 和翻译文件。
4. 本地至少运行一次基础语法检查：

```powershell
python -m compileall custom_components
```

## Bug 反馈

提交 issue 时请尽量提供以下信息：

- Home Assistant 版本
- 集成版本
- 投影仪型号
- 配置方式
- 复现步骤
- 相关日志

如果需要开启调试日志，可在 `configuration.yaml` 中加入：

```yaml
logger:
  logs:
    custom_components.dangbei: debug
```

## Pull Request

PR 请尽量说明下面几点：

- 改动解决了什么问题
- 是否存在兼容性影响
- 是否修改了文档或翻译
- 本地做了哪些验证

建议使用清晰的提交前缀，例如：

- `fix: ...`
- `feat: ...`
- `docs: ...`
- `refactor: ...`
- `chore: ...`
