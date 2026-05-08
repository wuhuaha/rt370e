# 执行计划模板

这份模板用于需要跨多个提交、多个模块、多个会话或板端多轮验证的工作。
目标不是把文档写长，而是让 Codex 和人类协作者都能快速回答四个问题：

1. 当前到底在做什么
2. 明确不做什么
3. 下一刀应该落在哪里
4. 每一步怎样验证

## 使用规则

- 适用场景：
  - 涉及多个模块
  - 预计超过一个提交
  - 需要多轮板端验证
  - 需要跨会话继续推进
- 不适用场景：
  - 单文件小修
  - 一次提交即可闭环的局部修复
- 新计划建立后，要同步更新：
  - `.codex/active_plans.md`
  - 如它成为当前主目标，再更新 `.codex/active_context.md`

## 推荐文件名

`doc/<TOPIC>_EXECUTION_PLAN_ZH.md`

例如：

- `doc/ORVIBO_AUDIO_STABILITY_EXECUTION_PLAN_ZH.md`
- `doc/KWS_RUNTIME_CLEANUP_EXECUTION_PLAN_ZH.md`

## 模板

````md
# <计划标题>

Status: active
Last Updated: YYYY-MM-DD
Branch: `<branch>`

## 1. 当前背景

- 当前主问题：
- 已知稳定基线：
- 当前默认 SDK：
- 与本计划强相关的现有文档：

## 2. 目标

- 

## 3. 非目标

- 

## 4. 约束 / Guardrails

- 

## 5. 已知事实

- 

## 6. 风险与未知项

- 

## 7. 执行切片

### Step A: <名称>

目标：

- 

范围：

- `path/to/file`

完成标准：

- 

验证：

```bash
<exact command>
```

期望结果：

- 

### Step B: <名称>

目标：

- 
````

## 编写建议

- 优先写“下一步怎么做”，不要把历史背景写成大段综述。
- 目标、非目标、完成标准都要可验证。
- 每个切片尽量对应一个可提交、可回滚、可板端验证的步骤。
- 验证命令尽量写完整命令，不要写“按常规方式验证”。
