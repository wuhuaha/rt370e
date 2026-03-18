# WebRTC AEC 调试计划与执行指引

## 1. 目的

本文用于指导当前 `debug/webrtc-aec` 分支上的 WebRTC AEC 实验验证，明确：

- 当前 AEC 实验的前提条件
- 在什么情况下可以认为“只是链路打通”
- 在什么情况下才可以认为“真正验证了 AEC 声学效果”
- 板端应如何分阶段调试
- 看哪些日志、哪些现象、哪些统计字段
- 什么结果可以继续推进，什么结果应立即止损或回退

本文是当前实验分支的执行指引，而不是产品最终方案。

## 2. 当前实验前提

当前实验 profile 为：

```text
capture(3ch: mic0 + mic1 + ref)
-> webrtc_aecm(experimental, gated)
-> fixed_dsb
-> silero_vad
-> streaming asr
```

当前输入模型已经收敛为：

```text
mic0 + mic1 + ref(native_capture_ch3)
```

当前运行时门控条件为：

- `PlaybackService` 处于活动态
- `InteractionState` 允许 AEC
- `ref_activity == active`

因此需要明确：

- **没有播放时，只能验证链路与状态机，不能真正验证 AEC 效果**
- **只有存在真实 far-end playback 且 ref 同步有效时，才有意义评估 AEC**

相关代码：
- [river_voice_preproc_fixed_dsb.c](/root/ameba-river/components/river_voice/river_voice_preproc_fixed_dsb.c)
- [river_voice_runtime_policy.c](/root/ameba-river/components/river_voice/river_voice_runtime_policy.c)
- [river_playback_service.c](/root/ameba-river/components/river_voice/river_playback_service.c)

## 3. 调试目标分层

当前 AEC 调试应分成 4 个层级，不能一步到位。

### 3.1 Level 0：配置与运行链验证

目标：

- 确认实验 profile 真被选中
- 确认采集已切到 `3ch`
- 确认日志真实反映链路，而不是历史残留文案

这一步不评估 AEC 效果，只确认：

- profile 正确
- 输入模型正确
- 输出链条没错

### 3.2 Level 1：参考信号有效性验证

目标：

- 确认 `native_capture_ch3` 确实存在
- 能区分：
  - `missing`
  - `idle`
  - `active`
- 参考状态变化符合预期

这一步仍不意味着 AEC 一定有效，只证明：

- 参考通道不是假的
- 参考状态机不是瞎跳

### 3.3 Level 2：AEC 运行时门控验证

目标：

- 验证 AEC 是否只在正确条件下开启
- 验证没有播放时是否稳定旁路
- 验证失败时是否平滑回退到 `fixed_dsb`

这一步的结论应该是：

- 当前门控逻辑可靠
- 即使 AEC 不工作，主链也不被污染

### 3.4 Level 3：AEC 声学效果验证

目标：

- 验证 far-end echo 是否被明显压制
- 验证 near-end 人声没有被过度损伤
- 验证双讲时 AEC 没有明显破坏 ASR

只有完成这一步，才能谈 “AEC 是否值得继续做”。

## 4. 当前最关键的工程判断

### 4.1 没有真实播放，就无法真正验证 AEC

当前如果设备没有实际播放内容，只能验证：

- profile
- ref 状态
- gate 状态
- 输出是否稳定 fallback
- 资源占用是否可接受

但不能验证：

- 回声是否被压制
- 双讲时近端语音是否保留
- barge-in 是否真的受益

### 4.2 因此调试必须先有“可控的播放源”

建议板端至少具备一个 AEC 调试播放源：

- 固定中文 PCM
- 或固定噪声音频
- 或固定测试短句

并且：

- 同一份播放数据必须可被 `PlaybackService` 观察到
- 当前实验 profile 下必须能驱动 `PlaybackState`
- 板端日志里能看到 `playback_state` 从 `idle` 进入活动态

如果当前还没有这个播放源，那么当前只能做 Level 0/1/2，不能做 Level 3。

## 5. 推荐调试阶段

### Phase A：静态链路确认

目标：

- 确认当前板端运行的是实验 profile

期望日志：

- `capture profile: ... 3ch ... +REF(native ch3)`
- `preproc backend: fixed_dsb + webrtc_aecm [experimental native-3ch-ref]`
- `preproc dsb+aec: profile=fixed_dsb_webrtc_aecm ...`

如果这里不对，后续所有 AEC 测试都无效。

### Phase B：无播放条件下的参考状态验证

目标：

- 确认没有播放时，系统不会误开 AEC

期望现象：

- `gate=playback_inactive`
- `ref_state=missing` 或 `ref_state=idle`
- `used=0`
- `fallback` 持续增加

判定：

- 如果没有播放但 `gate=active`，说明门控有 bug
- 如果没有播放但 `used` 持续增加，说明 AEC 被误开

### Phase C：播放状态验证

前提：

- 必须能触发真实播放

目标：

- 确认 `PlaybackService` 状态进入活动态
- 确认 `gate` 不再被 `playback_inactive` 阻塞

期望日志：

- `playback state=preparing/running/draining`
- `webrtc_aecm gate=...`
- gate 原因不再是 `playback_inactive`

判定：

- 如果播放已开始，但 gate 仍长期卡在 `playback_inactive`，说明播放服务状态源不对

### Phase D：参考活跃性验证

前提：

- 已有真实播放

目标：

- 确认 `ref_state` 能随播放变化进入 `active`

期望日志：

- `webrtc_aecm ref_state=active ... peak=... ratio_q15=...`
- `preproc aecm stats: gate=active ref_state=active ...`

判定：

- 如果播放期间 `ref_state` 长期为 `missing`，说明 ch3 ref 没进来
- 如果播放期间 `ref_state` 长期为 `idle`，说明 ref 能量门限或输入质量有问题
- 如果 `ref_state` 高频抖动，说明滞回参数还不够稳

### Phase E：Far-end only 验证

前提：

- 设备播放测试音频
- 人不说话

目标：

- 验证 AEC 在“纯回声”场景下是否在工作

观察重点：

- `gate=active`
- `used` 明显增加
- `fallback` 不应持续主导
- `aec_out` 不应异常发散

当前项目如果没有离线导出 `ref/mic/aec_out`，就先用业务现象判断：

- 纯播放期间，不应把扬声器回放当成近端语音持续送给 ASR

### Phase F：Near-end only 验证

前提：

- 无播放
- 人对设备说话

目标：

- 验证 AEC 路径不会在无 ref 时误伤近端语音

期望现象：

- `gate=playback_inactive`
- 或 `ref_missing/ref_idle`
- 输出仍为稳定 `fixed_dsb fallback`
- ASR 不因实验 profile 而明显劣化

### Phase G：Double-talk 验证

前提：

- 设备播放测试音频
- 人同时说话

目标：

- 验证 AEC 是否既压回声，又不过度伤害近端语音

这是最关键的业务场景。

观察重点：

- `gate=active`
- `used` 显著大于 `fallback`
- ASR 仍能识别人声
- 不应出现 near-end 被压成空文本或大幅漂移

### Phase H：Barge-in 验证

前提：

- 设备在播报
- 用户打断说话

目标：

- 验证播报中是否还能稳定进入语音交互

观察重点：

- `InteractionState` 是否允许 AEC
- `gate=active`
- VAD 是否还能进入 `speech`
- ASR 是否仍能得到有效文本

## 6. 必看日志字段

当前板端调试建议固定观察这几类日志。

### 6.1 前端链路确认

- `capture profile`
- `preproc backend`
- `preproc dsb+aec`

### 6.2 AEC 门控状态

- `webrtc_aecm gate=...`
- `playback state=...`
- `interaction state=...`
- `reference state=...`

### 6.3 AEC 运行态统计

- `preproc aecm stats: gate=...`
- `ref_state=...`
- `ratio_q15=...`
- `peak=...`
- `zero_streak=...`
- `aligned=...`
- `used=...`
- `fallback=...`
- `push_fail=...`
- `pop_fail=...`
- `resets=...`

### 6.4 系统资源日志

- `river.stats snapshot reason=...`

重点看：

- `heap_free`
- `heap_min`
- `cpu_top`
- `stack_free`

## 7. 调试记录模板

每次调试建议固定记录下面这些信息：

### 基本信息

- commit
- branch
- 当前 profile
- 是否开启实验 AEC
- 是否有真实播放源
- 测试距离
- 测试内容

### 日志结论

- `capture profile`
- `preproc backend`
- `gate` 最终状态
- `ref_state` 最终状态
- `used/fallback`
- `push_fail/pop_fail/resets`

### 业务结论

- 近端说话是否正常
- 播放中说话是否正常
- ASR 是否有空文本
- 是否有明显语义漂移

## 8. 当前建议的通过标准

### 8.1 Level 0 通过

- profile/log/capture channels 全部匹配

### 8.2 Level 1 通过

- `ref_state` 能稳定区分 `missing / idle / active`

### 8.3 Level 2 通过

- 无播放时稳定 `fallback`
- 有播放且 ref 活跃时才进入 `active`
- 失败时能平滑回退，不污染主链

### 8.4 Level 3 通过

- far-end only 时回声被明显压制
- near-end only 时近端语音不受明显损伤
- double-talk 时 ASR 仍有明显收益

## 9. 当前建议的止损标准

出现以下任一情况，建议暂停继续声学调参，先修链路：

- 无播放时 `gate=active`
- 有播放时长期 `ref_missing`
- `ref_state` 高频抖动
- `used` 长期为 0
- `fallback` 长期主导且原因不明
- `push_fail/pop_fail/resets` 持续累积
- AEC 打开后 ASR 明显比 `fixed_dsb` 更差

## 10. 推荐执行顺序

建议严格按以下顺序推进：

1. Phase A：静态链路确认
2. Phase B：无播放状态验证
3. Phase C：播放状态验证
4. Phase D：参考活跃性验证
5. Phase E：far-end only
6. Phase F：near-end only
7. Phase G：double-talk
8. Phase H：barge-in

不要跳步直接讨论音质或 ASR 结果，否则很容易把“链路没通”和“算法效果差”混为一谈。
