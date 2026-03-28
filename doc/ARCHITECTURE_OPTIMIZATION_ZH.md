# ameba-river 架构优化方案（中文）

## 1. 当前代码现状

当前项目已经具备以下基础能力：
- `Wi-Fi` 自动连接与重试
- `Silero VAD` 板端运行
- `fixed_dsb` 前处理接口边界
- 在线 ASR provider 框架
- 科大讯飞 provider 初步接入

当前主问题不是“有没有链路”，而是“链路耦合过深，调试面过宽”：
- 板端采集、VAD、云端流式连接、时间同步、provider 协议细节交织在一起
- 一处问题会被误判成另一处问题
- 现有日志虽多，但缺少“控制面”和“数据面”隔离

## 2. 站在顶级架构师视角的判断

### 2.1 当前架构的优点
- 已经形成明确边界：
  - `river_voice_capture`
  - `river_voice_preproc`
  - `river_voice_detector`
  - `river_cloud`
- 已经具备替换 provider 的基础能力
- 已经具备流式 / 非流式双入口的雏形

### 2.2 当前架构的主要问题
- `provider` 协议契约不稳定：
  - 讯飞新旧接口混用
  - WebSocket host/path/query 处理不一致
- `preproc` 语义被污染：
  - 历史上曾出现 `aivoice_afe [BYPASS MODE]`
  - 当前已切为软件 `fixed_dsb`，后续应继续避免“模块名和真实运行算法不一致”
- `voice -> cloud` 直推路径耦合仍偏重：
  - 语音检测与云流激活耦合过多
  - 缺少独立的“session gate”
- `调试工具不足`：
  - 缺少板外 host 侧协议验证工具
  - 导致云端问题和板端问题混在一起排

## 3. 对最近同事改动的审视

## 合理点
- 引入异步 ASR 管线和双环形缓冲，方向是对的
- 增强 Wi-Fi 生命周期控制，方向也是对的
- 为了系统余量曾暂时引入 `AFE BYPASS MODE`，在 bring-up 阶段有调试价值

## 不合理点
- 修改了用户拥有的 review/tips/issues 文件，不符合当前仓库协作规则
- 讯飞 provider 仍沿用旧 RTASR 接口模型，与当前官方文档不匹配
- WebSocket 使用方式不符合 Realtek SDK 客户端约束
- 历史上的 `aivoice_afe`/`bypass` 语义混杂增加了理解成本，现已收敛到 `fixed_dsb`

## 调整原则
- 不推翻同事的异步管线思路
- 保留其提升系统余量的方向
- 只修正：
  - provider 协议实现
  - 调试工具缺失
  - 误导性命名和观测问题

## 4. 推荐目标架构

### 控制面
- `river_wifi_station`
- `river_time_sync`
- `river_cloud_session_manager`
- `river_asr_provider_registry`

### 数据面
- `river_voice_capture`
- `river_voice_preproc`
- `river_voice_detector`
- `river_voice_stream_bridge`
- `river_cloud_asr_provider`

### 调试面
- host 侧 API 调试工具
- 板端 provider 状态转储
- 统一日志层
- 可切换的 provider/mock provider

## 5. 下一阶段优化路线

### Phase A：稳定当前链路
- 修正讯飞 provider 到新接口
- 固化 host 调试工具
- 让板端只承担：
  - 采集
  - VAD
  - 音频上送

### Phase B：抽出 session gate
- 在 `speech -> stream_open -> feed -> finish` 之间加独立会话状态机
- 避免 VAD 状态和云连接状态直接耦合

### Phase C：继续收敛真正的前处理 profile
- 当前主前处理已切到软件 `fixed_dsb`
- 后续如需恢复更复杂前处理，应以独立 profile 引入，而不是复用误导性名称

### Phase D：为多平台 ASR 做 provider 抽象收敛
- 统一握手、错误码、partial/final、关闭语义
- 板端代码只看标准事件，不看厂商细节

### Phase E：为在线 ASR + 本地 KWS/命令词做融合
- 本地 VAD/KWS 负责开门
- 云端 ASR 负责最终识别
- 未来可加离线 fallback
