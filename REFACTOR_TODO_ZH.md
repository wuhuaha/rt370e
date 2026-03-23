# Refactor TODO

日期：2026-03-23

## 已完成

- [x] 抽离 `river_app.c` 中的运行时会话编排，建立 `river_session_coordinator`
- [x] 把 bootstrap 与 runtime session policy 分层
- [x] 完成第一刀后的全量固件编译验证
- [x] 建立项目级架构蓝图和阶段计划

## 进行中

- [ ] 建立 cloud 内部边界：`river_cloud_internal.h`
- [ ] 抽离 xiaozhi session / conversation window policy
- [ ] 把 `river_cloud_adapter.c` 收敛为 façade

## 下一阶段

- [ ] 拆分 xiaozhi transport/runtime worker
- [ ] 拆分 provider-independent ASR bridge
- [ ] 清理 cloud 内部状态拥有权，减少重复 reset / close 路径

## Voice Runtime

- [ ] 拆分 `river_voice_kws.cc` 的 queue/gate/runtime ownership
- [ ] 把 KWS 诊断输出与热路径逻辑分离
- [ ] 拆分 `river_voice_vad_probe.c` 的运行时与诊断面

## Interface Hygiene

- [ ] 统一 public header 与 private internal header 边界
- [ ] 复查组件级 callback 接口命名和责任
- [ ] 禁止内部实现继续泄露到 `include/river/`

## Reliability / Performance

- [ ] 复查 reopen 路径中的 heap 占用与碎片风险
- [ ] 复查 wake session open/close 的重复触发与 coalesce 行为
- [ ] 复查 playback / barge-in / follow-up 的状态迁移闭环
