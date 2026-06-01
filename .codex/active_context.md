# Codex Active Context

This file is the canonical volatile context for Codex-facing work in this
repository. Update it when the working branch, active objective, SDK baseline,
or top-of-tree verification target changes.

## Active Working Set

- Current working branch: `xiaozhi-client`
- Active SDK baseline: `/root/ameba-rtos`
- Active build command:
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; export CMAKE_BUILD_PARALLEL_LEVEL=1; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'`
- Active flash command:
  - User-run board validation unless the user explicitly asks Codex to flash in
    the current turn:
    `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 /root/ameba-river/tools/river_flash.py -p /dev/ttyUSB0 -b 1500000'`
- Active monitor command:
  - User-run board validation unless explicitly requested in the current turn:
    `python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000`
- Latest landed step:
  - `Step H.xiaozhi-client.106 修正 KWS PCM/frontend 对拍工具`
- Current active objective:
  - Rebuild this branch as an Orvibo voice client mainline. The first external wire contract remains XiaoZhi-compatible, but code/file/function naming and runtime ownership are Orvibo-owned.
- Active plan:
  - `doc/ORVIBO_CLIENT_REARCH_EXECUTION_PLAN_ZH.md`
  - `doc/ORVIBO_DISPLAY_TOUCH_LVGL_EXECUTION_PLAN_ZH.md`
- Active plan index:
  - `.codex/active_plans.md`

## Current Architecture

- `app/` only boots `river_orvibo_app_boot()`.
- `components/river_core/` contains Orvibo app orchestration and the Orvibo state machine.
- `components/river_cloud/` contains Orvibo protocol/MCP transport plus shared Wi-Fi/WebSocket/Opus helpers.
- `components/river_voice/` contains current capture, preproc, Silero VAD, KWS, AEC/BF, playback, and reference services.
- `components/river_ui/` contains the Orvibo LVGL UI facade, ST7102 MIPI/LCDC bring-up path, and first-stage Sitronix touch probe.
- `components/river_diag/` exposes Orvibo, audio, playback, KWS tensor dump, and KWS alignment diagnostics.

## Latest Verified Slice

- `Step H.xiaozhi-client.106` 修正 KWS PCM/frontend 对拍工具：
  - `capture_kws_pcm_dump.py` 增加旧镜像 preproc-only 兼容模式 `--allow-missing-raw`，chunk 拉取按 64B 固件 chunk 长度过滤并支持重试，抓到 dump 后默认先发 `river orvibo abort` 降低会话/TTS 日志穿插概率。
  - `compare_board_pcm_frontend.py` 在旧日志 `begin` 行被串口打坏时可从 `frontend_contract.json` 推导 shape；新增 `--model` 可选 TFLite 打分；board-like frontend 改为使用固件里的 TFLM-style 分段 mel 标尺，训练侧 torchaudio path 继续使用 HTK mel。
  - 已在当前 `/dev/ttyUSB0` 旧镜像上完成一次 live preproc-only 抓取：`seq=6`，`feat_f32=503`、`output_raw=1`、`preproc_s16=1013`，PCM hash `0x7c488712` 与板端 meta 一致。
  - host 从同窗 `preproc_s16` 重算 board-like frontend 与板端 feature 精确对齐：`mae=0.000000`、`rmse=0.000000`、`max_abs=0.000003`、`corr=1.000000`；同一 PCM 的训练侧 torchaudio frontend 与板端 feature 差异明显，最佳候选 `mae=0.334050`、`corr=0.907431`。
  - FP32 TFLite 打分：板端 feature `0.551604`，host board-like feature `0.551603`，训练侧候选 `0.568944..0.569661`。当前证据更支持 frontend contract 差异/模型质量问题，不支持“模型二进制或 TFLite invoke 损坏”。
  - 当前板上仍是旧镜像，无 `raw_capture_s16` chunk；刷入 Step 105+ 镜像后需重新运行默认 capture 命令完成 raw/preproc 对拍。
  - `python3 -m py_compile`、脚本 `--help`、`git diff --check`、live preproc-only capture 和 compare + TFLite scoring 均通过；本步只改 host Python 工具和 `.codex` 记录，未执行 firmware build。

- `Step H.xiaozhi-client.105` 增强 KWS dump 紧凑串口格式：
  - `river kws dump meta` 继续输出原 `kws tensor dump ...` 日志，并额外输出 `KWSDUMP BEGIN/META/PCM_META/RAW_PCM_META/SNAPSHOT`；chunk 拉取输出改为更短的 `KWSDUMP CHUNK label=... seq=... chunk=... hex=...`，降低长串口日志被其它任务日志插断后无法解析的概率。
  - `tools/kws/replay_board_tensor_dump.py` 增加 compact PCM meta 解析；`tools/kws/capture_kws_pcm_dump.py` 同时识别普通 snapshot/chunk 和 `KWSDUMP` snapshot/chunk。
  - 本地 synthetic compact dump 已覆盖 `feat_f32 + output_raw + preproc_s16 + raw_capture_s16` 的完整解析与 compare；`board_numpy_from_pcm_union` 与 `board_numpy_from_raw_ch0_union` 均精确回到 synthetic 板端 feature hash。
  - `python3 -m py_compile`、脚本 `--help`、synthetic compare、`git diff --check` 和 `/root/ameba-rtos` 完整 build 均通过；当前容器仍未暴露串口设备，未执行 live serial capture。

- `Step H.xiaozhi-client.104` 增加 KWS PCM dump 串口抓取脚本：
  - 新增 `tools/kws/capture_kws_pcm_dump.py`，在设备已刷入 Step 103 镜像且串口可见时，自动发送 `river kws dump clear/next/meta/chunk ...`，拉取 `feat_f32`、`output_raw`、`preproc_s16`、`raw_capture_s16` chunk，并调用 `compare_board_pcm_frontend.py --require-raw` 生成 raw/preproc/frontend 对拍结果。
  - 当前容器没有 `/dev/ttyUSB*` 或 `/dev/ttyACM*`，因此本步未执行 live flash/serial capture；镜像仍需板子手动进入 download mode 后才能刷入。
  - `python3 -m py_compile`、脚本 `--help`、`git diff --check` 和 Codex harness check 均通过；未改固件代码，沿用 Step 103 的完整 firmware build 结果。

- `Step H.xiaozhi-client.103` 增加 KWS 同窗 raw/preproc PCM 对拍：
  - `river kws dump next` 在保留既有 `feat_f32`、`input_raw`、`output_raw` pull-based tensor dump 的同时，额外捕获同一次 inference 的 `preproc_s16` 与 `raw_capture_s16` PCM snapshot。
  - `raw_capture_s16` 由 Orvibo audio service 随同一帧 `preproc_s16` 提交到 KWS worker 队列，避免音频线程和 KWS worker 队列延迟导致 raw/preproc 错位。
  - PCM 抓取点按 KWS worker 实际处理样本推进；frontend reset 时同步记录 `center` 左侧 synthetic zero pad，所以 raw/preproc snapshot 与同一次 `feat_f32` 的前端窗口对齐。
  - 当前 A 2s FP32 变体的 `preproc_s16` 为 `32400` samples / `64800` bytes；2ch `raw_capture_s16` 为 `32400` frames / `129600` bytes，可通过 `river kws dump chunk preproc_s16 <index>` 和 `river kws dump chunk raw_capture_s16 <index>` 拉取。
  - 新增 `tools/kws/compare_board_pcm_frontend.py`，用于解析同一串口日志中的 `feat_f32 + preproc_s16 + raw_capture_s16`，校验 raw/preproc PCM hash，导出 WAV/NPY，并重算 board-like frontend 与训练侧 torchaudio 默认 frontend 近似路径。
  - `python3 -m py_compile`、`git diff --check` 和 `/root/ameba-rtos` 完整 build 均通过；尚未 flash/serial 抓取 live PCM，当前 NAND 硬件仍需手动进入 download mode 后才能刷入此镜像。

- `Step H.xiaozhi-client.102` 落地 A 2s FP32 唤醒模型代码：
  - 当前 HEAD 已包含 Step 101 的 UI 动画清理记录；本步在该 HEAD 之后提交 KWS 实际代码改动，避免 Step 100 记录与提交顺序混淆。
  - 本步代码内容与 Step 100 记录的部署结论一致：新增 A 2s FP32 generated header，新增 A 2s FP32 Kconfig 变体，`river_voice_kws.cc` 切到 `float32 [1,40,201,1]`，`prj.conf` 选择该变体并设置 `threshold_q15=14720`、`hold=1`、`fallback=off`、`pre_roll_ms=2000`、`pre_roll_flush=125`、`queue=192`。
  - 重新确认 staged diff 只包含 `.codex` 记录、KWS Kconfig、KWS runtime、`prj.conf` 和新模型 header；未额外纳入 UI 文件。
  - `/root/ameba-rtos` 完整 build 已通过并输出 `Build done`；生成 `.config` 和 AP KWS 预处理产物确认新模型与运行参数生效，旧 Teacher B BNT5 variant 字符串未出现在 AP KWS 编译产物中。
  - 未执行 flash/serial monitor；按当前 NAND 硬件策略等待用户手动上板验证。

- `Step H.xiaozhi-client.101` 删除灯/窗帘相关 UI 动画代码与资源：
  - 通过 `git log` 与代码检索确认，灯/窗帘 UI 动画入口主要来自提交 `afaaeeb 关联TTS动作文本与设备动画`，涉及 `river_orvibo_ui.c`、`assets/action_candidates/`、`assets/noto_cat_lvgl/generate_noto_cat_lvgl.py`、`assets/noto_cat_lvgl/river_noto_cat_anim.c` 和其 README。
  - `components/river_ui/river_orvibo_ui.c` 已删除 `开/关 + 灯/帘` 的 TTS 动画匹配逻辑；TTS 文本更新现在只刷新文本，不再切换 `action_light_*` / `action_curtain_*` 设备动画。
  - `components/river_ui/assets/action_candidates/` 下灯泡 / 窗帘 GIF 已删除；未跟踪的 `components/river_ui/assets/action_candidates_review/` 候选目录也已清理。
  - `components/river_ui/assets/noto_cat_lvgl/generate_noto_cat_lvgl.py` 已移除 4 个 `action_*` 资产定义，并重生成 `river_noto_cat_anim.c` / `README.md`；当前生成器输出回到 10 个猫表情动画、每个 8 帧、raw payload `2048000` bytes。
  - `rg` 与资源文件列表确认 `components/river_ui/` 和 `include/` 已无灯/窗帘 UI 动画 token、别名或 GIF 源资源引用。
  - 本步尚未 flash/serial monitor；后续按当前硬件策略仅保留用户手动上板验证。

- `Step H.xiaozhi-client.100` 接入 A 2s FP32 唤醒模型：
  - 按用户要求在 `/root/kws-trainint` 重新拉取算法仓库；算法仓库从 `d2b30f8` 快进到 `10f845c 记录A二秒bundle远端交付检查点`，新增 `student_conv_resnet_ed_nano_teacher_a_new_target_cycle24_2s_v1` 完整 bundle。
  - 固件侧新增 FP32 模型头 `components/river_voice/generated/student_conv_resnet_ed_nano_teacher_a_new_target_cycle24_2s_v1_fp32_model_data.h`，与算法 bundle `model_fp32_data.h` SHA256 一致：`73e66dbb1bdbb8d81089a437214f5f42ccfec8b8625c49d26eda0df1e3b67a18`；原始 `model.fp32.tflite` 为 `141700 B`，SHA256 `537af97f8a49ea651b34f02181de84182df767ddecd3cd7d6f4da58cdd95d71a`。
  - `Kconfig` 新增 `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_CONV_RESNET_ED_NANO_TEACHER_A_NEW_TARGET_CYCLE24_2S_V1_FP32_DEBUG`，并放宽 KWS VAD pre-roll、pre-roll flush、input queue 范围以支持 2s 前端。
  - `river_voice_kws.cc` 新变体使用 FP32 header，端侧 TFLite 契约为 `float32 [1,40,201,1] -> float32 [1,1,1,1]`，前端保持 `16k`、`n_fft/win_length=400`、`hop=160`、`center=true`、natural log、per-clip mean/std normalize。
  - `prj.conf` 当前选择 A 2s FP32 变体，停用 Teacher B BNT5 变体；部署阈值按 bundle `threshold_profiles.json` 推荐 `default_target_recall`：`threshold_probability=0.449219` / `threshold_q15=14720`，保持 `hold=1`、`cooldown=2500ms`、`fallback=off`、`stride=16`。
  - 因当前 KWS VAD gate open 会 reset frontend，本步把 pre-roll 扩到 `CONFIG_RIVER_KWS_VAD_PRE_ROLL_MS=2000`，并设置 `CONFIG_RIVER_KWS_PRE_ROLL_FLUSH_MAX_FRAMES=125`、`CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES=192`，确保 2s 模型 gate 打开后拿到完整历史上下文。
  - `/root/ameba-rtos` 完整 build 通过；生成 `.config` 确认新 A 2s FP32 变体、`14720/hold=1/fallback=off/pre_roll=2000/flush=125/queue=192` 生效，AP 预处理产物确认运行日志会打印新 variant 和同一组参数，且未编入旧 Teacher B BNT5 variant 字符串。
  - 未执行 flash/serial monitor；按当前 NAND 硬件策略等待用户手动上板验证。

- `Step H.xiaozhi-client.99` 审计 A 2s FP32 部署依赖并确认缺少模型产物：
  - 用户要求将当前固件模型切到 `student_conv_resnet_ed_nano_teacher_a_new_target_cycle24_2s_v1` FP32，并按算法文档部署参数。
  - 算法文档确认该模型的端侧契约为 FP32 `[1,40,201,1]`，bundle FP32 SHA256 应为 `537af97f8a49ea651b34f02181de84182df767ddecd3cd7d6f4da58cdd95d71a`，默认 profile 为 `threshold_probability=0.449219` / `threshold_q15=14720`，`recall_990` profile 为 `threshold_q15=13696`。
  - 当前 `/root/kws-trainint/artifacts/exports` 没有 `student_conv_resnet_ed_nano_teacher_a_new_target_cycle24_2s_v1/`，Git LFS 当前引用和 `git lfs fetch --all origin` 后的本地对象中也没有上述 FP32 SHA。
  - 当前 `/root/kws-trainint/artifacts/models` 没有该 student 的 `model_state.pt` 或 `checkpoints/best.pt`，无法在本机从 checkpoint 重建 FP32 TFLite/header。
  - 尝试用 `river_kws_gpu_cu128` 环境执行算法文档的 `export-student-model` 命令，导出在加载 `torchaudio` 时因缺少 `libcudart.so.13` 失败，且尚未进入 checkpoint 加载阶段。
  - 因缺少真实 `model.fp32.tflite` / `model_fp32_data.h`，本步未修改固件 KWS 变体或 `prj.conf`，避免留下不可构建或伪模型接入；后续需要算法同事提供完整 2s bundle，或修复本机导出环境并补齐 student checkpoint 后再继续。

- `Step H.xiaozhi-client.98` 再次刷新算法仓库并确认 student 全量导出更新：
  - 在 `/root/kws-trainint` 再次执行 `git pull --ff-only && git lfs pull`；当前算法仓库为 `main...origin/main`，HEAD 为 `d2b30f8 修正student导出runbook窗口提示`，本轮 pull 返回 `Already up to date`。
  - 相比 Step 97 记录的 `bc5bac5`，算法仓库后续增加 `ec78ef4 补充学生模型全量导出部署说明` 和 `d2b30f8 修正student导出runbook窗口提示` 两个提交。
  - 本次后续更新没有新增 `params/students/*.json`；当前新目标硬件 student 矩阵仍是 6 个：A/B 1s nano 加 4 个 2s 候选。
  - 新文档明确 6 个 student 均已有可复现的 INT8 + FP32 bundle 规格与推荐 INT8 profile，且端侧阈值必须来自 bundle 内 `threshold_profiles.json`，不要直接使用 PyTorch report 阈值。
  - `src/kws_training/student_export.py` 更新 runbook 文案：风险提示现在按实际 input shape 输出 `40x101` 或 `40x201`，并在 runbook 中展示推荐 INT8 profile；`tests/test_student_export.py` 增加 2s shape 风险提示测试。
  - 当前本地 Git/LFS 拉取后 `/root/kws-trainint/artifacts/exports` 仍只存在 1s A/B nano 新目标导出目录，没有 2s bundle 目录；如果要固件导入 2s student，仍需先在算法仓库重跑导出命令生成本地产物。
  - 本步未修改固件模型、`prj.conf`、VAD、tensor dump、alignment replay、board/local parity、UI 或云端协议路径；未执行 firmware build/flash/serial monitor。

- `Step H.xiaozhi-client.97` 刷新算法仓库并审计 2s student 候选：
  - 按固化流程在 `/root/kws-trainint` 执行 `git pull` 和 `git lfs pull`；算法仓库从 `f2ca5db` 快进到 `bc5bac5`，`git lfs status` 无待提交对象，状态为 `main...origin/main`。
  - 本次新增 4 个 `params/students/*2s_v1.json` 候选配置，均为新目标硬件数据口径 `teacher_a_new_target_hardware_v1`：
    - `student_conv_resnet_ed_nano_teacher_a_new_target_cycle24_2s_v1`
    - `student_conv_resnet_ed_tiny_teacher_a_new_target_cycle24_2s_v1`
    - `student_conv_resnet_ed_nano_teacher_b_new_target_bnt5_2s_v1`
    - `student_conv_resnet_ed_tiny_teacher_b_new_target_bnt5_2s_v1`
  - 这些新配置均为 `2000ms` / `40` mel / `25ms` frame / `10ms` shift，导出后输入契约为 `[1,40,201,1]`；它们不能直接沿用当前固件的 `40x101` 1s 前端路径。
  - 算法文档 `docs/context/2026-06-new-target-hardware-student-deployment-guide.md` 同时把现有 A/B 1s nano 与新增 4 个 2s 候选合成 6 个新目标硬件 student 部署矩阵；文档说明模型二进制和 header 是本地生成物，不提交到 Git。
  - 当前本地 `/root/kws-trainint/artifacts/exports` 在 Git/LFS 拉取后没有新增 `*2s*` 导出目录；若要导入固件，需要先在算法仓库重跑对应 `export-student-model` 生成 bundle。
  - 本步未修改固件模型、`prj.conf`、VAD、tensor dump、alignment replay、board/local parity、UI 或云端协议路径；未执行 firmware build/flash/serial monitor。

- `Step H.xiaozhi-client.96` 回到 Teacher B 算法推荐触发配置：
  - 当前固件仍使用 `student_conv_resnet_ed_nano_teacher_b_new_target_bnt5_v1` FP32 debug 模型，未切到 INT8。
  - 按用户确认，当前项目不再通过阈值或 hold 调参处理误唤醒；误唤醒质量问题后续交由算法同事/模型迭代优化。
  - `prj.conf` 将 `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15` 从 `26214` 恢复为 Teacher B bundle `default_target_recall` 推荐值 `12928`（probability `0.394531`，运行时约 `threshold_pm=394`）。
  - `CONFIG_RIVER_KWS_TRIGGER_HOLD_FRAMES` 从 `3` 改为 `1`，对齐算法离线单次过阈值命中语义；`CONFIG_RIVER_KWS_COOLDOWN_MS=2500`、`CONFIG_RIVER_KWS_GATE_FALLBACK_EN=n`、`stride=16` 保持不变。
  - `AGENTS.md` 固化新协作规则：除非用户当前明确要求固件侧调参，KWS trigger threshold/hold 使用模型交付或算法同事推荐配置，误唤醒优化回到算法/模型路径。
  - 本步只改 KWS trigger 配置和项目记录，不更换模型文件，不修改 VAD、tensor dump、alignment replay、board/local parity、UI 或云端协议路径。
  - `git diff --check`、`python3 tools/diag/check_codex_harness.py`、`/root/ameba-rtos` 完整 build 均通过；构建输出为 `Build done`，生成 `.config` 确认 `12928/hold=1/fallback=off`，AP/combined image strings 仍确认 Teacher B FP32 变体生效。
  - 未执行 flash/serial monitor；按当前硬件策略等待用户手动上板验证。

- `Step H.xiaozhi-client.95` 收紧 Teacher B FP32 唤醒阈值和 hold：
  - 当前固件仍使用 `student_conv_resnet_ed_nano_teacher_b_new_target_bnt5_v1` FP32 debug 模型，未切到 INT8。
  - 用户 2026-06-01 实板日志确认部署正确，但非唤醒语音可在 `threshold_pm=394`、`hold=2` 下触发；样本包括 `603/654pm` 连续误触发和 `830/861pm` 两帧高分误触发。
  - `prj.conf` 现将 `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15` 提高到 `26214`（约 `threshold_pm=800`），并将 `CONFIG_RIVER_KWS_TRIGGER_HOLD_FRAMES` 提高到 `3`；`CONFIG_RIVER_KWS_COOLDOWN_MS=2500` 和 `CONFIG_RIVER_KWS_GATE_FALLBACK_EN=n` 保持不变。
  - 本步只改 KWS 参数，不更换模型文件，不修改 VAD、tensor dump、alignment replay、board/local parity、UI 或云端协议路径。
  - `git diff --check`、`python3 tools/diag/check_codex_harness.py`、`/root/ameba-rtos` 完整 build 均通过；构建输出为 `Build done`，生成 `.config` 确认 `26214/hold=3/fallback=off`，AP/combined image strings 仍确认 Teacher B FP32 变体生效。
  - 未执行 flash/serial monitor；按当前硬件策略等待用户手动上板验证。

- `Step H.xiaozhi-client.94` 部署 Teacher B new-target BNT5 FP32 唤醒模型：
  - 当前固件侧 KWS 已从 `student_conv_resnet_ed_nano_current_teacher_a_v2` 切到 `student_conv_resnet_ed_nano_teacher_b_new_target_bnt5_v1` 的 FP32 debug 变体；未切到 INT8。
  - 新增模型头文件 `components/river_voice/generated/student_conv_resnet_ed_nano_teacher_b_new_target_bnt5_v1_fp32_model_data.h`，与 `/root/kws-trainint/artifacts/exports/student_conv_resnet_ed_nano_teacher_b_new_target_bnt5_v1/model.fp32.tflite` 字节一致：`141700 B`，SHA256 `5ba71c42362ee9e4f93310166d95de74bcbe6a138548852372ba9daee5183e38`。
  - `Kconfig` 新增 `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_CONV_RESNET_ED_NANO_TEACHER_B_NEW_TARGET_BNT5_V1_FP32_DEBUG`；`prj.conf` 选择该变体，并设置 `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=12928`、`CONFIG_RIVER_KWS_TRIGGER_HOLD_FRAMES=2`、`CONFIG_RIVER_KWS_COOLDOWN_MS=2500`、`CONFIG_RIVER_KWS_GATE_FALLBACK_EN=n`。
  - Bundle 契约已复核为 FP32 `float32 [1,40,101,1] -> float32 [1,1,1,1]`，算子集合 `ADD/AVERAGE_POOL_2D/CONV_2D/LOGISTIC`，继续使用当前 `40x101` centered log-mel 前端和 conv-only resolver path。
  - `git diff --check`、`python3 tools/diag/check_codex_harness.py`、`/root/ameba-rtos` 完整 build 均通过；构建输出为 `Build done`，生成 `.config` 和 AP/combined image strings 已确认 Teacher B FP32 变体生效。
  - 未执行 flash/serial monitor；按当前硬件策略等待用户手动上板验证。

- `Step H.xiaozhi-client.93` 拉取 Teacher A/B 新端侧模型导出包：
  - 按固化流程再次在 `/root/kws-trainint` 执行 `git pull` 和 `git lfs pull`；仓库从 `f9d5cbf` 快进到 `f2ca5db`，`git lfs status` 无待提交对象，状态仍为 `main...origin/main`。
  - 最新关键交付提交为 `601e94a 交付Teacher A/B端侧模型导出包`，新增两个同规格端侧 nano bundle：`student_conv_resnet_ed_nano_teacher_a_new_target_cycle24_v1` 和 `student_conv_resnet_ed_nano_teacher_b_new_target_bnt5_v1`。
  - Teacher A bundle：INT8 `44992 B` / SHA256 `6f15f06c1fc65c2d239b48c265a6e416fcd9273ee35b97c9e5c222c4a03d0ff9`，FP32 `141700 B` / SHA256 `caf5b5f853e0ce889395dfda12667d3dade7764edc9b37fa23a5e6fc7175afe9`，默认档 raw int8 `-36`，board recall `0.966790`，board FA/h `554.787969`。
  - Teacher B bundle：INT8 `44992 B` / SHA256 `33fe5c8f3d176d532c047650e32ab9c64a9a53c365b9787483447b7257ec0bd7`，FP32 `141700 B` / SHA256 `5ba71c42362ee9e4f93310166d95de74bcbe6a138548852372ba9daee5183e38`，默认档 raw int8 `-27`，board recall `0.940959`，board FA/h `597.831863`。
  - 两个 bundle 均保持 `[1, 40, 101, 1]` 输入和 `ADD/AVERAGE_POOL_2D/CONV_2D/LOGISTIC` 算子集合，且 `formal_export_gate_passed=False`；算法侧建议 Teacher A 作为主试板候选，Teacher B 作为对照候选。
  - 固件侧当前仍只接入 `student_conv_resnet_ed_nano_current_teacher_a_v2`，尚未新增 Teacher A/B 的 generated header 或 `prj.conf` 变体。
  - 未执行 firmware build/flash/serial monitor；本步只刷新和审计算法仓库。

- `Step H.xiaozhi-client.92` 刷新并审计算法仓库唤醒模型：
  - 按 `AGENTS.md` 固化流程在 `/root/kws-trainint` 执行 `git pull` 和 `git lfs pull`；算法仓库工作区保持干净，状态为 `main...origin/main`，`git pull` 返回 `Already up to date`。
  - 当前算法仓库最新提交为 `f9d5cbf 新增同规格端侧nano候选以降低替换迁移风险`，没有比本地更新的远端模型提交。
  - 最新候选包为 `student_conv_resnet_ed_nano_current_teacher_a_v2`：INT8 TFLite `44992 B` / SHA256 `16130fff3bbc478c0bf19cae8fb1b66e2873f5090a35fe9cb6f9bfb8b95bac5d`，FP32 TFLite `141700 B` / SHA256 `6434ed43c722428459f90ac13571acf8aa348ae1f19fb1e6a291da55c962b9aa`。
  - 模型契约为 `[1, 40, 101, 1]`、`ADD/AVERAGE_POOL_2D/CONV_2D/LOGISTIC`，默认档 `default_target_recall` 为 probability `0.290451` / Q15 `9517` / int8 `-54`；算法包仍标记 `formal_export_gate_passed=False`。
  - 固件侧 `prj.conf` 已选择 `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_CONV_RESNET_ED_NANO_CURRENT_TEACHER_A_V2_FP32_DEBUG=y`，对应生成模型头文件已存在；本步无需重新导入模型文件。
  - 未执行 firmware build/flash/serial monitor；本步只刷新和审计算法仓库。

- `Step H.xiaozhi-client.90` 新增 Orvibo 单轮对话模式：
  - 新增 `CONFIG_RIVER_ORVIBO_SINGLE_TURN_MODE`，`prj.conf` 默认启用单轮对话：唤醒后完成一次服务端请求/响应，收到服务端 TTS stop 并等待本地 playback drain 后关闭 realtime WebSocket，回到 `IDLE` 本地唤醒监听。
  - `river_orvibo_state_machine` 增加单轮/连续模式运行时开关和 `conversation_mode` 状态名；`SPEAKING + SERVER_TTS_FINISHED` 在单轮模式下执行 `WAIT_PLAYBACK_IDLE`、`CLOSE_AUDIO_CHANNEL`、`AUDIO_IDLE`、`DISABLE_BARGE_IN`，连续模式保留旧的 post-TTS `LISTENING` 行为。
  - 启动日志和 `river orvibo status` 增加 `conversation_mode`；诊断新增 `river orvibo mode <status|single|continuous>` 便于上板确认或临时切换。
  - `git diff --check`、单轮配置/API/诊断/state grep、`/root/ameba-rtos` 完整 build、生成配置检查、AP image string check 和 `python3 tools/diag/check_codex_harness.py` 均通过；未执行 flash/serial monitor，按当前硬件策略等待用户手动上板验证。

- `Step H.xiaozhi-client.89` 关联 TTS 智能家居文本与动作动画：
  - 新增 `components/river_ui/assets/action_candidates/`，下载并记录 Wikimedia Commons 来源的灯泡亮灭 GIF 和幕布开合 GIF，生成器离线转成 `action_light_on/off`、`action_curtain_open/close` 四个 `lv_animimg` 动画 token。
  - `noto_cat_lvgl` 生成器扩展为通用 UI GIF 资源生成器，当前生成 14 个动画、每个 8 帧、`80x80`、`LV_COLOR_FORMAT_ARGB8888`/BGRA，总 raw payload 约 2.8MB。
  - `river_orvibo_ui.c` 在 TTS 文本更新时按顺序匹配 `开...灯`、`关...灯`、`开...帘`、`关...帘`，命中后切换到对应动作动画；ASR、状态机和协议 wire format 不变。
  - `river_lvgl_port.c` 将小 caption 从 `CAT` 改为 `ANIM`，兼容猫表情和设备动作动画。
  - `git diff --check`、生成器 `py_compile`、资源重生成和 `python3 tools/diag/check_codex_harness.py` 均通过；未执行完整 build/flash/serial monitor，用户明确说明同事正在构建，避免干扰共享 build 目录。

- `Step H.xiaozhi-client.88` 关联语音交互状态与猫表情动画：
  - `river_orvibo_ui.c` 将状态表情收敛为集中规则：`starting/idle` 中性猫脸，`network_wait/connecting` 微笑等待，`listening` 放松笑脸，`speaking` 开心猫，`recovering/error` 分别为生气/惊吓。
  - LLM emotion 现在走白名单映射，仅对 `relaxed/happy/excited/love/thinking/sad/angry/surprised/...` 等已知值覆盖状态表情；未知 emotion 回退到最新 voice state 的表情。
  - 新增 `river ui state <starting|network|idle|connecting|listening|speaking|recovering|error>` 诊断命令，用于不依赖云端的上板动画状态切换验证。
  - `noto_cat_lvgl` 生成器和生成 C 文件把 `idle` alias 归到 `noto_cat_face_1f431`，与空闲状态设计保持一致。
  - `git diff --check`、生成器 `py_compile`、`python3 tools/diag/check_codex_harness.py` 和 `/root/ameba-rtos` 完整 build 均通过；AP image 确认包含 `river ui state ...`、状态动画 key、`lv_animimg_*` 和 `river_noto_cat_anim_*`；未执行 flash/serial monitor，按当前硬件策略等待用户手动上板验证。

- `Step H.xiaozhi-client.87` 接入 Noto 猫表情 LVGL 动画播放：
  - 新增 `components/river_ui/assets/noto_cat_lvgl/` 固件资源目录；10 个 Noto cat GIF 均离线转为 `80x80`、每个动画 8 帧、`LV_COLOR_FORMAT_ARGB8888`/BGRA 的 LVGL `lv_image_dsc_t` 帧。
  - `river_lvgl_port.c` 新增 `lv_animimg` 表情对象，按 UI emoji token/emotion alias 切换动画并循环播放，同时保留小号 `CAT ...` caption；不启用 SDK `LV_USE_GIF`，也不依赖运行时文件系统。
  - `river_orvibo_ui.c` 将 Orvibo 状态默认 emoji 切到 Noto cat key：idle/listening 用 `noto_smiley_cat_1f63a`，speaking 用 `noto_joy_cat_1f639`，recovering/error 分别用 pouting/scream。
  - `river ui emoji <key>` 诊断别名可直接切换 `smiley/smile/joy/heart/smirk/kissing/pouting/crying/scream/face`，用于上板快速验证完整系列。
  - `git diff --check`、生成脚本 `py_compile`、`python3 tools/diag/check_codex_harness.py` 和 `/root/ameba-rtos` 完整 build 均通过；AP image 确认包含 `lv_animimg_*`、`river_noto_cat_anim_*` 和 10 个 Noto cat key；未执行 flash/serial monitor，按当前硬件策略等待用户手动上板验证。

- `Step H.xiaozhi-client.86` 下载完整 Noto 猫表情动态图系列：
  - 用户选定 `noto_smiley_cat` 风格后，删除上一轮 Pixabay GIF 候选，只保留 Noto animated cat 风格。
  - 候选目录现在包含 10 个 Noto cat GIF：`1f431`、`1f638`、`1f639`、`1f63a`、`1f63b`、`1f63c`、`1f63d`、`1f63e`、`1f63f`、`1f640`。
  - 重新生成 `preview_contact_sheet.png`，并更新资产 README 的源 URL、license/source note、尺寸/帧数/大小和 SHA-256。
  - 当前 `LV_USE_GIF=0`，本步仍只归档 raw GIF 候选；后续应选择运行时表情并离线转成 LVGL image descriptors。
  - Python/Pillow frame scan、SHA-256、Pixabay 删除检查、`git diff --check` 和 `python3 tools/diag/check_codex_harness.py` 均通过；未执行 firmware build/flash/serial monitor。

- `Step H.xiaozhi-client.85` 下载猫表情动态图候选资源：
  - 新增 `components/river_ui/assets/emoji_candidates/` 独立候选资源目录。
  - 下载 3 个 AnimatEmojis / Google Noto animated cat emoji GIF：`1f63a`、`1f638`、`1f63b`，页面标注 Google 和 CC BY 4.0。
  - 下载 2 个 Pixabay cute cat GIF：`pixabay_cat_cute_emoji_6939.gif`、`pixabay_cat_cute_tickle_6937.gif`，源页面有较高浏览/下载/收藏数据，文件更小，适合作为首轮固件候选。
  - 新增候选资源 README，记录源 URL、license/source note、尺寸/帧数/大小、选择原因、SHA-256 和后续固件转换边界。
  - 当前 `LV_USE_GIF=0`，本步只归档 raw GIF 候选；后续应选择候选并离线转成 LVGL image descriptors，或单独启用并验证 GIF decoder。
  - `file`、Python/Pillow frame scan、SHA-256、`git diff --check` 和 `python3 tools/diag/check_codex_harness.py` 均通过；未执行 firmware build/flash/serial monitor。

- `Step H.xiaozhi-client.84` 切换 Orvibo 默认服务端到自建 Phase 1：
  - 依据 `doc/device-integration-manual.md`，默认 OTA/config 地址切到 `http://101.33.235.154:8082/xiaozhi/ota/`；OTA 返回的 websocket URL 应为 `ws://101.33.235.154:8082/xiaozhi/v1/`。
  - 新增 `CONFIG_RIVER_ORVIBO_AUTHORIZATION_VALUE="orvibo-river"`，当前服务端不校验该值，但 OTA HTTP 和 WebSocket handshake 都会携带 Authorization header。
  - OTA HTTP header 现在包含 `Protocol-Version`、`Device-Id`、`Client-Id`、`Authorization`，WebSocket header 在 OTA token 为空时使用默认 Bearer 占位值。
  - 本步不改变 hello/listen/audio/MCP wire format，不修改 VAD/KWS、tensor dump、alignment replay、board/local parity、Wi-Fi、音频、UI 或 SDK 源码。
  - `git diff --check`、`python3 tools/diag/check_codex_harness.py` 和 `/root/ameba-rtos` 完整 build 均通过；image string check 确认新 OTA 地址/Authorization 存在且无 `api.tenclass`；未执行 flash/serial monitor，按当前硬件策略等待用户手动上板验证。

- `Step H.xiaozhi-client.83` 修复 Orvibo LVGL 中文显示：
  - 根因是 ASR/TTS label 仍使用 `LV_FONT_DEFAULT`，而当前 AmebaSmart LVGL 默认字体是 `lv_font_montserrat_14`，服务端中文 UTF-8 到达正常但字体无 CJK glyph。
  - SDK 自带 CJK 子集不是默认字体，且缺少本轮日志和智能家居回复中的若干关键简体字；本步不修改 SDK，而是在项目内新增 `river_lv_font_zh_16`。
  - `components/river_ui/river_lv_font_zh_16.c` 是 16px/bpp=2 SourceHanSansSC LVGL 字体子集，覆盖当前 ASR/TTS 日志关键字、常用智能家居词、ASCII 和中文标点。
  - `components/river_ui/CMakeLists.txt` 在 `CONFIG_RIVER_UI_LVGL_EN` 下编译项目字体，`river_lvgl_port.c` 将 ASR/TTS label 切到该字体。
  - `river_orvibo_ui.c` 增加 UTF-8 安全截断，避免长中文句子在 192 字节 UI 消息缓冲区边界截断半个字符。
  - `git diff --check`、字体关键字覆盖 grep、AP archive/image `nm` 字符号检查和 `/root/ameba-rtos` 完整 build 均通过；未执行 flash/serial monitor，按当前硬件策略等待用户手动上板验证。

- `Step H.xiaozhi-client.82` 接入 Orvibo LVGL 显示和触摸探测：
  - 新增 `components/river_ui` 和 `include/river/river_orvibo_ui.h`，Orvibo core/app 只通过 UI facade 投递 state、emotion/emoji、ASR/STT 和 TTS text，不直接操作 LVGL。
  - LVGL 9 端口创建 480x480 初始页面，显示 state、ASCII emotion/emoji、ASR、TTS 和 touch 摘要；第一阶段不引入 CJK/emoji 字库闭合。
  - 项目内实现 ST7102 power/reset/backlight、MIPI DSI init table、LCDC RGB565 flip；默认 `CONFIG_RIVER_UI_ST7102_MIPI_LANE_NUM=1`，保留 1/2 lane 可切换以便上板验证供应商 init 与原理图差异。
  - Sitronix touch 第一阶段只做 `PB10/PB11` I2C、`PA9` INT、`PA10` RST 的 reset/probe 和候选地址 ACK 扫描，坐标报文解析留到实板 ACK/register 验证后闭合。
  - 新增 `river ui status`、`river ui touch scan`、`river ui text <asr|tts|emoji> <text>` 诊断命令。
  - SDK LVGL demo objects 会随 `CONFIG_LVGL_ENABLE` 被 AP/HP 链接；本步新增项目侧 AP 兼容符号和 HP no-op 兼容组件解链接，不修改 `/root/ameba-rtos`。
  - `git diff --check`、`python3 tools/diag/check_codex_harness.py` 和 `/root/ameba-rtos` 完整 build 均通过；未执行 flash/serial monitor，按当前硬件策略等待用户手动上板验证。

- `Step H.xiaozhi-client.81` 补齐硬件报告关键器件和接线图谱：
  - 本地 `schematic-pcb-firmware-guide` skill 增加“关键器件身份归一化”和“引脚接线图谱”流程，要求不能把 PDF/OCR raw value 直接当最终型号，要结合 refdes、管脚、封装、网名、周边电路、本地 datasheet、用户资料和外部资料推断。
  - skill 模板新增 `Firmware Engineer Entry Map`、`Critical Component Matrix`、`Pin Wiring / Net Connection Atlas` 和 `External Reference Library`，并要求 HTML 同步提供可搜索/过滤的器件表、接线表和来源可追溯图示。
  - `doc/SCHEMATIC_PCB_FIRMWARE_GUIDE_SKILL_ZH.md` 同步中文规则，明确像 `U/AXS2033/QFN8/AXS` 这类 raw value 应归一化为型号 `AXS2033`、封装/丝印上下文和功能角色，而不是用固定正则硬拆。
  - 使用最新规则完整重生成 `doc/RTL8730E_4INCH_HARDWARE_FIRMWARE_GUIDE_ZH.md` 和同名 HTML，新增固件工程师入口地图、关键器件矩阵、引脚接线图谱、外部参考资料库、音频/LCD/NAND/BL702 路径图和 HTML 搜索/排序/checklist。
  - 报告显式覆盖关键型号和接线：`RTL8730EAM`、`MSM261DDB021`、`AXS2033`、`GD5F1GM7UEYIGR`、`A113F-15025WUA-R01`、`STI9287C`、`BL702C-10-Q2H`、`TMI3411`、`TMI6050-33`、`EY404-CF42F1`、`FPC512-10-RL-TA-01`，并把 `PA2/PA4 DATA1 -> DMIC3/DMIC4`、`LINEOUT_LN/LP -> AXS2033 -> CN2`、`PB25 -> MUTE/SD`、CN6 DSI/touch/backlight、U11 QSPI NAND 和 BL702 UART/boot/reset 路径写成可执行 handoff。
  - 本步只改文档和本地 skill，不修改固件源码、SDK、Kconfig、构建脚本、烧录 profile、VAD/KWS、tensor dump、alignment replay、board/local parity 或协议逻辑。
  - HTML static parser、离线依赖检查、关键型号/章节 grep、scope grep、helper `py_compile`、`git diff --check` 和 `python3 tools/diag/check_codex_harness.py` 均通过；未执行固件 build/flash。

- `Step H.xiaozhi-client.80` 优化 Orvibo TTS 播放抗欠载：
  - `orvibo_tts` 播放 period 从 16 个 60ms 应用帧收敛为 4 帧，降低 Ameba AudioTrack IRQ 路径中大 DMA period 带来的 underrun/xrun 风险。
  - 保持 `defer_start_until_prefilled=false`，因为历史板端验证已证明该 SDK 路径不支持“先 write 再 start”，会导致 `AudioTrack write: invalid state(1)`。
  - 播放运行中新增低水位静音桥接，只在 track 已 start、buffer 低于阈值且有实际空余时补 1 帧静音；阈值会按 SDK 实际 buffer 容量收敛。
  - TTS 写入失败时增加一次本地 stop/start + retry 当前帧；TTS playback reference history 扩到 10s，降低 `playback ref overflow` 噪声。
  - audio diag/status 新增 `recover` 和 `gap=ok/fail` 计数，方便后续上板判断桥接与恢复是否触发。
  - `git diff --check`、deferred-start guard grep 和 `/root/ameba-rtos` 完整 build 均通过；未执行 flash/serial monitor，按当前硬件策略等待用户手动上板验证。

- `Step H.xiaozhi-client.79` 为硬件报告 skill 增加交互式 HTML 伴随文档：
  - 本地 `schematic-pcb-firmware-guide` skill 默认输出从单一 Markdown 扩展为 Markdown + 同名离线交互式 HTML。
  - skill 模板新增 `Interactive HTML Companion` 要求：HTML 自包含、内容与 Markdown 证据编号一致、无 CDN/远程字体/外部框架，并包含 sticky TOC、搜索/过滤、可折叠章节、可排序表格、copy 命令、`localStorage` checklist 和来源可追溯 SVG/CSS 可视化。
  - `doc/SCHEMATIC_PCB_FIRMWARE_GUIDE_SKILL_ZH.md` 同步中文规则，明确 HTML 不允许额外扩展业务层、产品交互、云端协议、会话策略或 UI 行为。
  - 新增 `doc/RTL8730E_4INCH_HARDWARE_FIRMWARE_GUIDE_ZH.html`，覆盖概览指标、PDM 输入路径图、speaker/AXS2033 输出路径图、LCD/touch 拓扑图、NAND/BL702 启动边界图、证据表、缺失/假设、快速上手命令、避坑表、pin map、外设块、board-validation checklist、风险和 scope audit。
  - `doc/RTL8730E_4INCH_HARDWARE_FIRMWARE_GUIDE_ZH.md` 增加 HTML 伴随文档入口，`doc/README.md` 增加 HTML 索引。
  - 本步只改文档和本地 skill，不修改固件源码、SDK、Kconfig、构建脚本、烧录 profile、VAD/KWS、tensor dump、alignment replay、board/local parity 或协议逻辑。
  - HTML static parser、Markdown/HTML evidence parity、scope grep、Playwright 本地 `file://` 交互检查、desktop/mobile screenshot、helper `py_compile`、`git diff --check` 和 `python3 tools/diag/check_codex_harness.py` 均通过；未执行固件 build/flash。

- `Step H.xiaozhi-client.78` 为硬件报告 skill 增加避坑/上手建议并重生成报告：
  - 本地 `schematic-pcb-firmware-guide` skill 增加 `Extract pitfalls and getting-started advice` 流程，要求每份硬件固件 handoff 报告提炼证据可追溯的“避坑指南 / 注意事项 / 上手建议”。
  - skill 模板新增 `Quick Start For Firmware Engineers` 和 `Pitfalls / Attention Points / Getting Started Advice`，每条建议需要包含为什么容易错、证据、固件影响、可信度和第一步低风险检查。
  - `doc/SCHEMATIC_PCB_FIRMWARE_GUIDE_SKILL_ZH.md` 同步记录中文使用规则，强调这部分内容用于提高开发效率、降低踩坑概率、提升 BSP/HAL/driver 代码质量，不写泛泛经验或产品/业务行为。
  - 使用新规则完整重生成 `doc/RTL8730E_4INCH_HARDWARE_FIRMWARE_GUIDE_ZH.md`，新增快速上手和避坑表，覆盖音频、NAND、LCD/touch、BL702、TH、IR、RF 等高概率错误点。
  - 本步只改文档和本地 skill，不修改固件源码、SDK、Kconfig、构建脚本、烧录 profile、VAD/KWS、tensor dump、alignment replay、board/local parity 或协议逻辑。
  - `pdf_artifact_inventory.py` 重新清点 `doc/hard/`，report structure/scope 检查、helper `py_compile`、`git diff --check` 和 `python3 tools/diag/check_codex_harness.py` 均通过；未执行固件 build/flash。

- `Step H.xiaozhi-client.77` 按硬件报告复核音频适配并调满输出音量：
  - 使用最新版硬件固件说明书复核语音输入/输出绑定：输入保持已验证 `PDM_CLK=PA2`、`PDM_DAT1=PA4`、DATA1、`AUDIO_DMIC3/DMIC4`、`pdm-2mic-pa2-pa4-data1`；输出保持 `Audio HAL speaker/LINEOUT -> LINEOUT_LN/LP -> AXS2033 -> CN2`，功放控制为 `PB25/MUTE`。
  - 本轮没有发现报告缺口阻塞音频播放实现，因此未继续修改 skill 或硬件报告正文。
  - `self.audio_speaker` 本地默认音量改为 `100`；Orvibo audio open 在 `AudioService_Init()` 后设置硬件音量 `1.00f/1.00f`；下行播放 `volume_left/right`、`river playback tone` 和 echo 调试路径硬件音量均改为 `1.00f`。
  - `/root/ameba-rtos` 完整 build 通过；AP image 字符串和 AP Audio HAL 预处理确认 PA2/PA4/PB25 覆盖仍生效。
  - 按用户本轮明确要求尝试 `/dev/ttyUSB0` NAND 烧录；工具打开串口但在进入下载模式阶段失败：`Enter download mode fail: ErrType.SYS_PROTO`，未开始写 flash。
  - 本步不修改 SDK 源码、采集拓扑、VAD/KWS、tensor dump、alignment replay、board/local parity、协议 wire format 或业务/会话策略。

- `Step H.xiaozhi-client.76` 从零重生成硬件固件说明书并收敛报告边界：
  - 使用最新版 `schematic-pcb-firmware-guide` 和 `pdf` 辅助流程重新清点 `doc/hard/`，并重新渲染/复核电源、RTL8730E、PDM/IR/LINEOUT/AXS2033、LCD/touch/backlight、GPIO/NAND、BL702/Zigbee 和 TH FPC 关键页。
  - `doc/RTL8730E_4INCH_HARDWARE_FIRMWARE_GUIDE_ZH.md` 已完整替换为面向 BSP/HAL/driver 的硬件 handoff 报告，包含证据表、资料索引、缺失澄清、假设方案、pin map、外设块、bring-up checklist、故障特征、风险和 scope audit。
  - 报告正文不包含 skill 自身实现、agent 工作过程、提示词、迭代记录或工具开发 changelog；这些内容仅记录在 skill/项目说明和正常变更记录中。
  - 报告继续覆盖已验证的 PDM `PA2/PA4 DATA1 -> DMIC3/DMIC4`、AXS2033 speaker/LINEOUT/PB25 SD、NAND、LCD/DSI/touch、BL702、TH、IR、Wi-Fi/BT RF、电源/复位/boot 和板级验证路径。
  - 业务层、产品交互、会话、UI、网络服务策略等只作为范围外说明出现，不作为硬件结论或开发方案。
  - 本步只改文档和项目内 skill 使用说明；不修改固件源码、SDK 源码、构建配置、VAD/KWS、tensor dump、alignment replay、board/local parity 或协议逻辑。
  - `git diff --check`、fresh report structure check、scope grep 和 `python3 tools/diag/check_codex_harness.py` 均已通过；未执行固件 build/flash/monitor。

- `Step H.xiaozhi-client.75` 接入本地音频播放诊断并绑定 PB25 功放控制：
  - AP Audio HAL override 现在同时覆盖数字麦 PA2/PA4 和功放脚：`AUDIO_HW_AMPLIFIER_PIN=_PB_25`，匹配原理图 `PB25/MUTE -> AXS2033 SHUT/SD`。
  - `components/river_diag` 增加 audio interface include path，并给 `river playback` 新增纯本地 `tone [freq_hz] [duration_ms] [level_pct]` 诊断命令。
  - 默认验证命令为 `river playback tone 1000 1000 25`；该命令走 `AudioService_Init`、`DEVICE_OUT_SPEAKER`、`river_playback_service`、16kHz/2ch/16-bit 本地 PCM，不涉及云端、业务播放或会话流程。
  - 本轮按用户临时要求没有搜索外部资料；报告/skill 迭代只基于当前报告、repo、`/root/ameba-rtos` SDK 和本地构建产物。
  - `schematic-pcb-firmware-guide` 新增实现绑定点要求：仓库/SDK 可见时，报告必须写出 override header、build hook/target、Kconfig/HAL API、诊断命令和建议归属源码文件。
  - `doc/RTL8730E_4INCH_HARDWARE_FIRMWARE_GUIDE_ZH.md` 已补充 `_PB_25` 绑定、tone 命令、预期日志和 U7 pin1 `SHUT/SD` 测量点。
  - `/root/ameba-rtos` 完整 build 通过；final AP image 含 `river playback tone`/`diag_tone`/`PB25/MUTE`；AP Audio HAL 预处理确认 `board_amp_pin = (0x39)`、`amp_info.pinmux = (0x39)`。
  - 未执行烧录和串口 monitor；当前 NAND 硬件策略仍要求用户手动上板验证，除非用户在当前回合明确要求 Codex 烧录。

- `Step H.xiaozhi-client.74` 收敛硬件报告边界并补齐 LCD/功放说明：
  - 根据用户反馈，报告边界明确为硬件资料到 BSP/HAL/driver 的交接，不擅自定义云端、业务协议、产品交互或应用层播放/会话方案。
  - 本地 `schematic-pcb-firmware-guide` skill 新增 report boundary 和 final scope audit 规则，并增强 report template 的 hardware path、firmware boundary、BSP/HAL/driver interface、Scope Audit 和 audio/display/touch 完整性提示。
  - `doc/RTL8730E_4INCH_HARDWARE_FIRMWARE_GUIDE_ZH.md` 纳入用户补充的 `doc/hard/LCD/`：ST7102 480x480 init table、Sitronix touch driver 移植手册和 `ST_TDDI_TPDriver_v45.00.260402` 源码包。
  - 报告补齐 LCD/touch：ST7102 init table 的 `SSD_LANE(1,0)` 与原理图 D0/D1 两条 data lane 路由存在待确认冲突；Sitronix I2C `0x55` 仅作为源包候选地址，完整 lane rate/porch/reset/touch 地址仍需澄清。
  - 报告补齐 AXS2033 音频输出硬件路径、gain/输入高通/SD 电压区间/BTL 输出/回采网络，并指出本板 `MUTE=PB25` 与 SDK 默认 `AUDIO_HW_AMPLIFIER_PIN=_PB_19` 不匹配。
  - 新增 `.gitignore` 规则忽略 Windows `*:Zone.Identifier` 元数据，并用 `.gitattributes` 对 `doc/hard/LCD/**` 关闭 whitespace 检查，避免改写供应商原始资料格式。
  - 本步只改文档和本地 skill，不修改固件源码、SDK 源码、VAD/KWS、tensor dump、alignment replay、board/local parity、协议或运行策略。

- `Step H.xiaozhi-client.73` 生成 RTL8730E 4 寸板硬件固件说明书：
  - 使用 `schematic-pcb-firmware-guide` 和 `pdf` skill 分析 `doc/hard/` 下主原理图、丝印图、MSM261DDB021 PDM 麦 datasheet 和 AXS2033 功放 datasheet。
  - 新增报告 `doc/RTL8730E_4INCH_HARDWARE_FIRMWARE_GUIDE_ZH.md`，覆盖证据表、artifact inventory、缺失信息/澄清问题、假设/候选方案、MCU pin map、外设块、启动/验证 checklist 和风险列表。
  - 报告把 PA2/PA4 + DATA1 (`DMIC3/DMIC4`) 标为已验证音频采集方案，并把 LCD/touch、TH sensor、AXS2033 SD 电压、BL702 协议等未闭合项列入 clarifications。
  - 本次使用过程中迭代增强本地 skill：新增 `scripts/pdf_artifact_inventory.py`，用于无 Poppler 环境下批量清点 PDF/图片、提取文本、渲染指定页面。
  - 本步只改文档、skill 和 repo hygiene；不修改固件源码、SDK 源码、VAD/KWS、tensor dump、alignment replay、board/local parity 或协议逻辑。

- `Step H.xiaozhi-client.72` 增强原理图/PCB skill 的资料补充和缺失澄清流程：
  - `schematic-pcb-firmware-guide` 现在明确支持工程师提前提供 datasheet、SDK 示例、EDA 导出、设计笔记、运行日志、本地路径或 URL。
  - 报告模板新增工程师资料索引、缺失信息/澄清问题和假设/候选方案章节。
  - 对缺失内容要求列出影响、当前最佳猜测、可信度、建议 owner/check；对可靠猜测必须标 hypothesis 并给验证路径。
  - 本步不改固件源码或 SDK。

- `Step H.xiaozhi-client.71` 新增原理图/PCB 固件说明书分析 skill：
  - 本地 skill 路径：`/root/.codex/skills/schematic-pcb-firmware-guide`。
  - 目标流程：`PDF/图片 -> OCR/视觉分析 -> 外部 datasheet/reference manual/SDK 示例检索 -> 可追溯 Markdown 固件说明书`。
  - 项目文档：`doc/SCHEMATIC_PCB_FIRMWARE_GUIDE_SKILL_ZH.md`。
  - 本步不改固件源码或 SDK，只新增 Codex 本地能力和项目使用说明。

- `Step H.xiaozhi-client.70` 删除临时采集路径扫描，仅保留 PA2/PA4 DATA1 方案：
  - H.69 已实板验证 PA2/PA4 + DATA1 (`DMIC3/DMIC4`) + AP Audio HAL override 为有效语音路径：采集/预处理峰值非零，VAD/KWS、server STT/TTS 均已跑通。
  - 本步删除 `CONFIG_RIVER_VOICE_CAPTURE_PATH_SWEEP_*`、DATA0-3 boot sweep、sweep replay 日志和相关公开函数。
  - 当前固件只保留正常采集路径：`pdm-2mic-pa2-pa4-data1`、`AUDIO_DMIC3/DMIC4`、`AUDIO_HW_DMIC_CLK_PIN=_PA_2`、`AUDIO_HW_DMIC_DATA1_PIN=_PA_4`、`capture dmic pinmux applied: clk=PA2 data1=PA4`。
  - 当前回合只做构建验证；如需上板，按 Active flash/monitor command 手动下载和串口验证，或由用户再次明确要求 Codex 烧录。

- `Step H.xiaozhi-client.59` 收敛空响应的音频前端诊断：
  - 用户 2026-05-28 16:17 日志显示网络和激活已正常：固定 `device_id=00:e0:4c:b7:23:e2` 生效，Wi-Fi 获取 `192.168.3.32`，OTA 返回 `activation required=no`，并出现 `access refresh ok`。
  - 同段日志在用户说话时仍反复显示 `audio diag: mode=idle ... speech=no prob=48/47 ... kws=...`，说明当前问题已经转为本地 VAD/KWS 前端未检测到有效语音。
  - 新增 `CONFIG_RIVER_VOICE_DSB_PRIMARY_ONLY`，当前 `prj.conf` 启用主麦直通作为 fixed DSB mono 输出，规避双麦极性/声道映射不确定导致的相加抵消。
  - `river_orvibo_audio_service` 周期诊断新增原始采集三路峰值和预处理 mono 峰值，便于上板区分“麦克风没声”“预处理抵消”和“VAD/KWS 阈值/模型问题”。
  - 本步保留 VAD/KWS、tensor dump、alignment replay、board/local parity、KWS 模型/参数、AECM 实验代码、网络/激活/协议和 SDK 源码不变。
  - `/root/ameba-rtos` 完整 build 已通过；当前 NAND 硬件需要手动进入烧录模式，板端 runtime validation 由用户执行。

- `Step H.xiaozhi-client.58` 固定空 efuse 板 Orvibo Device-Id：
  - 用户 2026-05-28 13:40 日志显示服务端下发 code `514285` 时，access identity 为 `00:e0:4c:b7:23:e2`；用户添加该 code 后，2026-05-28 13:45 重启日志显示 Device-Id 又漂移为 `00:e0:4c:b7:23:1a`，服务端重新下发 code `379507` 并保持 HTTP 202 pending。
  - 根因收敛为当前空 efuse 板 runtime STA MAC 每次启动可能变化；如果 Orvibo `Device-Id` 直接跟随 runtime STA MAC，服务端绑定会漂移到不同设备。
  - 新增 `CONFIG_RIVER_ORVIBO_DEVICE_ID`，默认空值保持既有 runtime-MAC 行为；当前 `prj.conf` 固定为已在服务端添加过 code 的 `00:e0:4c:b7:23:e2`。
  - `river_orvibo_access_refresh_identity()` 优先使用配置的固定 Device-Id，并用相同 MAC 字节派生 `client_id`，不改变 activation payload 语义。
  - 本步不修改 Wi-Fi runtime MAC、Wi-Fi credential、扫描/连接策略、DHCP、Orvibo websocket token、音频链路、VAD、KWS、tensor dump、alignment replay、board/local parity、AEC/BF 或 SDK 源码。
  - 当前 NAND 硬件需要手动进入烧录模式；Codex 只构建并通知用户，不主动烧录或串口 monitor。

- `Step H.xiaozhi-client.57` 增强 Orvibo 激活绑定码诊断：
  - 用户 2026-05-28 11:53 板端日志显示，H.56 已解决 Wi-Fi bring-up 卡点：HP 空 efuse prompt 被 bypass，`wifi_on()` 返回，设备扫描并连接 `river`，DHCP 获取 `192.168.3.24`。
  - Orvibo access 已刷新真实 MAC identity `00:e0:4c:b7:23:68`，OTA 能应用 websocket config；当前剩余问题是服务端提示 `device activation required`。
  - `river_orvibo_access` 现在同时解析字符串型和数字型 `activation.code`，并单独打印 `activation bind code=...`，便于用户在服务器侧添加/绑定。
  - OTA activation 摘要现在打印 required/challenge/done/activation version/HMAC configured/code/message；`/activate` 轮询打印每次 HTTP result 与 error。
  - access refresh 失败后会立即 dump `orvibo access` 状态，方便从同一段串口日志判断 `ready/ws_config/activation/challenge/code/message/http/last_error`。
  - 本步不改变 activation payload 语义、不输出 HMAC key 或 websocket token，也不修改 Wi-Fi、协议、音频链路、VAD、KWS、tensor dump、alignment replay、board/local parity、AEC/BF 或 SDK 源码。
  - 当前 NAND 硬件需要手动进入烧录模式；Codex 只构建并通知用户，不主动烧录或串口 monitor。

- `Step H.xiaozhi-client.56` 绕过 HP 空 efuse 按键等待：
  - 用户 2026-05-28 11:02 板端日志显示，H.55 的 AP 侧 `wifi_init` wrapper 已生效，`sdk auto wifi_on skipped` 已出现，首次项目所有的 `wifi_on()` 进入后卡在 `whc_api=0x9`。
  - 同段日志 HP/KM4 侧打印 `[WLAN-E] Efuse empty! Wifi performance may be affected. Press any key to ignore and continue`，且 AP 侧 `wifi_on=pending`、`[INIC-A] cur id 0x9; latest id 0x9` 持续出现。
  - 新增项目侧 HP project hook，不修改 `/root/ameba-rtos` SDK 源码；HP `image2` 现在会引入 `components/river_wifi_hp_shim`。
  - HP shim 通过链接器 `--wrap=wifi_on`、`--wrap=LOGUART_INTConfig`、`--wrap=LOGUART_Readable`，仅在 HP `wifi_on()` 调用期间对空 efuse 按键等待返回一次可读状态。
  - 新增 HP 侧日志 `[river.wifi.hp] wifi_on start ... efuse_key_wait_bypass=armed`、`[river.wifi.hp] efuse key wait bypassed`、`[river.wifi.hp] wifi_on returned ... efuse_key_wait_bypass=...`。
  - 完整 `/root/ameba-rtos` build 已通过；HP image2 链接参数和符号均确认包含三个 wrap，AP image 仅保留既有 `__wrap_wifi_init` 且不包含 HP efuse bypass 字符串。
  - 本步未修改 SDK 源码、Wi-Fi credential、扫描/连接策略、协议、音频链路、VAD、KWS、tensor dump、alignment replay、board/local parity、AEC/BF 或烧录 profile。
  - 当前 NAND 硬件需要手动进入烧录模式；Codex 只构建并通知用户，不主动烧录或串口 monitor。

- `Step H.xiaozhi-client.55` 禁止 SDK 自动启动 Wi-Fi：
  - 用户 2026-05-28 10:09 板端日志显示，H.54 后 SDK 默认 Wi-Fi 配置已正确加载，但项目 STA 任务卡在 `wifi_is_running start attempt=1 wlan=0 whc_api=0x4`。
  - 同时 `[INIC-A] Host api ipc timeout: cur id 0x9; latest id 0x4` 持续出现，说明 SDK 自动 `wifi_on()` 已经占住 WHC API 等待链路，项目自己的查询被后续卡住。
  - 新增项目侧 `river_wifi_init_override.c`，通过 AP 镜像 `-Wl,--wrap=wifi_init` 拦截 SDK main 的 `wifi_init()`。
  - AP/WHC host 侧仍执行 `wifi_set_rom2flash()`、`LwIP_Init()`、`whc_host_init()`，但跳过 SDK init 线程里的自动 `wifi_on(RTW_MODE_STA)`；首次 `wifi_on()` 继续由 `river_wifi_station` STA 任务负责并打印既有 `whc_api=0x9` 边界日志。
  - HP/KM4 镜像仍保留 SDK 原生 `wifi_init` / `wifi_init_thread`，不改变 WHC device 侧初始化。
  - 完整 `/root/ameba-rtos` build 已通过；final AP image symbols include `__wrap_wifi_init` and `river_wifi_init_thread`，HP image symbols still include SDK `wifi_init` and `wifi_init_thread`。
  - 本步未修改 SDK 源码、Wi-Fi credential、扫描/连接策略、协议、音频链路、VAD、KWS、tensor dump、alignment replay、board/local parity、AEC/BF 或烧录 profile。
  - 当前 NAND 硬件需要手动进入烧录模式；Codex 只构建并通知用户，不主动烧录或串口 monitor。

- `Step H.xiaozhi-client.54` 修正 Wi-Fi SDK 默认配置加载顺序：
  - 用户 2026-05-28 09:36 板端日志仍未进入 `scan start` / `connect attempt`；`credential[0] ssid=river ssid_len=5 password_len=10` 已确认 Wi-Fi 凭据仍是 `river` 和 10 字节密码。
  - 新日志中的 `user cfg source=patch_before` 显示 `country=0000(..)`、`band=unknown(0x00)`，且 SDK 关键默认字段仍为 0，说明项目在 SDK 默认 `wifi_set_user_config()` 填充前就覆盖了 `wifi_user_config`。
  - `river_wifi_station_patch_user_config_once()` 现在先调用 SDK 弱符号 `wifi_set_user_config()`，打印 `sdk_defaults`，再叠加项目侧 country/band/tx power/reconnect/IPS/LPS 覆盖，避免完整 SDK 默认配置丢失。
  - Wi-Fi user config 日志新增 `concurrent_enabled`、`softap_addr_offset_idx`、`skb_num_np/ap`、`rx/tx_ampdu_num`、`ampdu_rx/tx_enable`、`ap_sta_num`、`wifi_wpa_mode_force`、`probe_hidden_ap_on_passive_ch`、shortcut、keepalive、no-beacon timeout。
  - STA 任务入口新增 `sta task started`；调用 `wifi_is_running(STA_WLAN_INDEX)` 前后会记录 `whc_api=0x4`、attempt、ret、elapsed，用于区分卡在 `wifi_is_running` 还是卡在 `wifi_on`。
  - `river_wifi_station_dump_status()` 增加 `wifi_task`、loop count、`wifi_is_running` 状态/attempt/ret/elapsed；上层每 3 秒 `waiting wifi` 后会同步 dump 详细 Wi-Fi 状态。
  - 本步未修改 Wi-Fi credential、密码内容、扫描/连接策略、DHCP、Orvibo 协议、音频链路、VAD、KWS、tensor dump、alignment replay、board/local parity、AEC/BF、烧录 profile 或 SDK 源码。
  - 当前 NAND 硬件需要手动进入烧录模式；Codex 只构建并通知用户，不主动烧录或串口 monitor。

- `Step H.xiaozhi-client.53` 将默认烧录模式切到当前 NAND 硬件：
  - 用户 2026-05-27 20:10 烧录失败日志显示 wrapper 选了 `RTL8730E_NOR.rdev`，而 Flash 工具实际探测到 `MemoryType: NAND`、GD5F1GM7U、`1Gb/128MB`。
  - 失败根因是 `Flash type mismatch: Device: 2 / Device Profile: 1`，即实际 NAND 硬件被 NOR profile 烧录，不是固件镜像或 Wi-Fi 初始化改动导致。
  - `tools/river_flash.py` 的 `--memory-type/-m` 默认值从 `nor` 改为 `nand`，匹配当前硬件；当前 NAND 烧录命令可直接使用 `python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000`。
  - `-m nand` 仍是等价显式写法；旧 NOR 硬件必须显式使用 `-m nor`。
  - 本步只修改主机侧烧录 wrapper 默认值和文档，不修改固件源码、烧录 profile 内容、SDK、Wi-Fi credential、协议、音频链路、VAD、KWS、tensor dump、alignment replay、board/local parity 或 AEC/BF。

- `Step H.xiaozhi-client.52` 增强 Wi-Fi 初始化阶段诊断：
  - 用户 2026-05-27 19:30 板端日志已经证明 H.51 的 credential/dump 日志生效，但没有出现 `connect attempt`、`scan start`、`scan ap[...]` 或 `connect strategy=...`，说明还没有进入项目侧扫描/连接阶段。
  - 同一段日志反复出现 `[INIC-A] Host api ipc timeout: cur id 0x9`；复核 `/root/ameba-rtos/component/wifi/whc/whc_def.h` 后确认 `WHC_API_WIFI_ON = BASIC_API_BASE + 9`，当前卡点更像 SDK `wifi_on(RTW_MODE_STA)` / WHC IPC 初始化。
  - 日志里的 `[WLAN-E] Efuse empty! Wifi performance may be affected...` 与 SDK 默认 `country_code=0x0000`、`tx_pwr_table_selection=2` 依赖 efuse 的配置相冲突，因此本步在项目侧显式设置空 efuse fallback。
  - Wi-Fi user config patch 前后和 `wifi_on` 前都会打印 country、band、tx power table selection、802.11d、EDCCA、fast/auto reconnect、IPS/LPS。
  - 项目侧 Wi-Fi fallback 现在设置为 `country_code='0''0'`、`freq_band_support=RTW_SUPPORT_BAND_2_4G_5G_BOTH`、`tx_pwr_table_selection=1`、`rtw_802_11d_en=0`，避免 SDK 继续 follow/depend on 空 efuse。
  - 新增 `wifi_on start ... whc_api=0x9` 和 `wifi_on returned ret=... elapsed_ms=...` 边界日志；如果用户烧录后仍只看到 start 看不到 returned，就可以确认仍卡在 SDK `wifi_on()` 内部。
  - `river_wifi_station_dump_status()` 增加 `wifi_on=not_done|pending|ok`、attempts、ret、elapsed_ms，方便从状态行判断初始化是否卡住。
  - 本步未修改 Wi-Fi credential、密码内容、扫描/连接策略、DHCP、Orvibo 协议、音频链路、VAD、KWS、tensor dump、alignment replay、board/local parity、AEC/BF 或 SDK 源码。
  - 当前 NAND 硬件需要手动进入烧录模式；Codex 只构建并通知用户，不主动烧录或串口 monitor。

- `Step H.xiaozhi-client.51` 增强 Wi-Fi 连接失败诊断日志：
  - 本步只修改 `components/river_cloud/river_wifi_station.c` 的诊断输出，不改变 Wi-Fi 连接策略、扫描策略、候选选择、重试节奏、credential 配置或网络协议行为。
  - credential 加载时打印 `ssid`、`ssid_len`、`password_len`，不打印密码明文；当前主配置应显示 `credential[0] ssid=river ssid_len=5 password_len=10`。
  - 扫描日志现在能看到前 8 个 AP 的 SSID/BSSID/channel/band/RSSI/security，并在目标 AP 命中时打印 candidate index、RSSI、信道、加密类型和 BSSID。
  - 连接尝试日志增加策略、SSID/密码长度、是否使用扫描候选、候选 RSSI、信道、加密类型和 BSSID；join 等待过程中按状态变化/每秒输出 join 状态。
  - 连接成功和 `river wifi/status` 相关 dump 会输出 PHY snapshot，包含 `rssi`、`data_rssi`、`beacon_rssi`、`snr`。
  - 当前 NAND 硬件需要手动进入烧录模式；除非用户当前回合明确要求，Codex 后续只做到构建完成并通知用户，不主动运行烧录或串口 monitor。
  - `/root/ameba-rtos` 完整 build 已通过，`git diff --check` 和 `python3 tools/diag/check_codex_harness.py` 已通过；板端运行验证等待用户手动烧录后观察日志。

- `Step H.xiaozhi-client.50` 为当前 NAND 硬件新增项目烧录 profile：
  - 当前硬件切换到 NAND 烧录路径，活跃烧录命令改为 `tools/river_flash.py ... -m nand`。
  - 从 `/root/ameba-rtos` 的 SDK stock `RTL8730E_NAND.rdev` 建立项目内可审阅 JSON 副本，并新增项目版 `RTL8730E_NAND.json` / `RTL8730E_NAND.rdev`。
  - NAND bootloader slot 保持 stock `0x00000000-0x00040000` 不变。
  - NAND 主应用下载区间从 stock `0x00040000-0x00300000` 扩到当前硬件要求的 `0x00040000-0x00C40000`。
  - 当前 `km0_km4_ca32_app.bin` 约 `0x399a80` bytes；从 `0x00040000` 下载后结束于约 `0x003d9a80`，会越过 SDK stock NAND 的 `0x00300000` 上限，但仍落在项目 NAND profile 的 `0x00C40000` 内。
  - `tools/river_flash.py -m nand` 默认复制 SDK Flash 工具到临时目录运行，并只提高临时 `Settings.json` 的 NAND 请求重试/写入等待参数，避免修改 SDK 本体。
  - 2026-05-27 已在当前 NAND 板上完成实际烧录：GD5F1GM7U NAND 全量 app 下载校验覆盖 `0x00040000-0x00C40000`，最终 `Finished PASS`。
  - 烧录日志中仍出现多次 `Negative response: b'\xe2'`，对应 NAND 写入路径的间歇性超时；项目 wrapper 的更高 retry budget 已能消化该现象。
  - 本步只改变项目烧录 profile/文档/活跃命令，不修改固件源码、Kconfig、协议、音频链路、VAD、KWS、tensor dump、alignment replay、board/local parity、AEC/BF 或 SDK 源码。
  - `RTL8730E_NAND.rdev` 已由项目 JSON 生成并反解确认 `EndAddress=12845056`，即 `0x00C40000`；`/root/ameba-rtos` 完整 build 和 NAND flash 实测均已通过。

- `Step H.xiaozhi-client.49` 屏蔽播放期软件 KWS 误触发 Orvibo TTS 打断：
  - 2026-05-09 16:22 板端日志显示 H.48 已生效，普通 VAD 在 speaking 期间只打印 `vad speech ignored during speaking`，没有直接触发状态切换。
  - 新的截断风险来自 speaking 期间软件 KWS gate 被 TTS/回声打开，随后出现连续高于阈值的 KWS 分数；按当前 `hold=2` 会触发 `WAKE_DETECTED` 并停止 TTS。
  - 对照 `~/xiaozhi-esp32/main/application.cc`，参考端 speaking 模式只允许 AFE wake word，普通/custom 软件 wake word 不在播放期运行。
  - Orvibo `SPEAKING` 模式现在固定关闭本地软件 KWS detection gate，reason 为 `orvibo_speaking_playback`，避免 TTS 播放自身进入软件唤醒词判定。
  - H.43 的 realtime-capable speaking 上行仍保留：barge-in enabled 且 profile 具备 AEC/native-ref 时继续编码上行；本步没有关闭采集、VAD、AEC 或服务端实时打断可能性。
  - 本步未修改 KWS 模型/阈值/hold/cooldown/特征/推理、tensor dump、alignment replay、board/local parity、VAD、AEC/BF、协议 wire contract、MCP、OTA/v2 激活或 Opus framing。
  - 风险是当前软件 KWS 不能在 speaking 期间本地打断 TTS；若后续要恢复，需要引入具备播放参考抑制能力的 AFE/hardware wake 或更强的软件确认机制。
  - `git diff --check`、`python3 tools/diag/check_codex_harness.py` 和 `/root/ameba-rtos` 完整 build 均已通过；仍需烧录后确认 TTS 期间不再出现 `kws gate open` / `wakeword hit`。
- `Step H.xiaozhi-client.48` 屏蔽普通 VAD 误触发 Orvibo TTS 打断：
  - 2026-05-09 15:47 板端日志显示，TTS 中途被截断的直接原因是 `orvibo state: speaking -> listening reason=vad_start event=user_speech_started actions=0xaa4`，即普通 VAD speech-start 在 speaking 阶段触发了 `STOP_PLAYBACK`。
  - 这次现象已经不是 H.47 的 playback drain 判据问题；截断发生在服务端后续 TTS 文本仍在到来期间，本地状态机先被 VAD barge-in 拉回 listening。
  - `river_orvibo_audio_handle_vad()` 现在只在 `LISTENING` 模式下发出 `SPEECH_STARTED`；`SPEAKING` 模式继续运行 VAD 和统计概率，但只累加 `vad_speaking_barge_suppressed` 并限频打印 `vad speech ignored during speaking`。
  - Orvibo 状态机同时移除 `SPEAKING + USER_SPEECH_STARTED -> STOP_PLAYBACK` 分支，防止未来其它路径绕过 audio service 再次用普通 VAD 中止 TTS。
  - 唤醒词/KWS 打断保持不变，`SPEAKING + WAKE_DETECTED` 仍会停止当前播放并重新 listening；H.43 的 realtime-capable speaking 上行也保持不变。
  - 本步未修改 VAD 模型/推理/阈值、唤醒词/KWS 模型/参数、tensor dump、alignment replay、board/local parity、AEC/BF、协议 wire contract、MCP、OTA/v2 激活或 Opus framing。
  - 风险是普通非唤醒词语音不再能本地立即打断 TTS；后续若需要该能力，必须增加比单次本地 VAD start 更强的确认机制。
  - `git diff --check`、`python3 tools/diag/check_codex_harness.py` 和 `/root/ameba-rtos` 完整 build 均已通过；仍需烧录后确认 TTS 期间不再出现 `speaking -> listening reason=vad_start`。
- `Step H.xiaozhi-client.47` 按播放呈现位置收敛 Orvibo TTS 尾部排空：
  - 对照 `~/xiaozhi-esp32` 与 `~/py-xiaozhi` 后确认，参考端不会在收到 `tts stop` 后立刻关闭/flush 输出设备，而是让播放队列或长期输出流自然排空。
  - 当前 Ameba 分支的上层仍会在 wait_idle 后执行 `Pause/Flush/Stop`，所以 wait_idle 必须代表硬件已经呈现完整尾部，而不是只看 SDK buffer 状态。
  - `/root/ameba-rtos` 复核确认 `AudioTrack_GetBufferStatus()` 更像 DMA buffer remain/status；`AudioTrack_GetPosition()` 通过 AmebaSmart presentation-position 路径反映从 track start 起已播放帧数，是更稳的 drain 判据。
  - playback service 现在统计所有成功 `AudioTrack_Write()` 接收的 `submitted_frames`，drain 时进入 `DRAINING`，写入一帧 tail silence pad，然后等待 `rendered_frames >= submitted_frames`。
  - 呈现帧数达标后增加 `180ms` tail grace，再允许 stop/flush；连续 5 次取不到 position 后才 fallback 到有效 buffer 状态，避免瞬时失败导致提前停流。
  - status/drain 日志暴露 `rendered/submitted` 和 `pos_fail`，板端应看到 `playback drain tail pad`、`tail grace`、`drain complete ... rendered>=submitted`。
  - 本步未修改 VAD、唤醒词/KWS、模型、tensor dump、alignment replay、board/local parity、AEC/BF、协议 wire contract、MCP、OTA/v2 激活或 Opus framing。
  - `git diff --check`、`python3 tools/diag/check_codex_harness.py` 和 `/root/ameba-rtos` 完整 build 均已通过；仍需烧录后确认 TTS 尾部完整播放。
- `Step H.xiaozhi-client.45` 主机侧验证 XiaoZhi-compatible v2 activation 可达性：
  - 本地配置检索未发现真实 `RIVER_ORVIBO_ACTIVATION_SERIAL_NUMBER` / `RIVER_ORVIBO_ACTIVATION_HMAC_KEY`，因此本步只使用占位 serial/HMAC 验证请求形态和服务器可达性。
  - OTA v2 POST 到 `https://api.tenclass.net/xiaozhi/ota/` 返回 HTTP 200，包含 `activation.code`、`activation.message`、`activation.challenge` 和 websocket 配置。
  - 使用占位 serial/HMAC 调 `/activate` 返回 HTTP 404，错误为 license 不存在或已激活；这说明服务器路径可达，但没有真实登记 license/key pair 时不能证明完整 v2 激活成功。
  - 使用 OTA 返回的 websocket URL/token 进行主机侧 WebSocket upgrade，服务端返回 `HTTP/1.1 101 Switching Protocols`。
  - 本步只做验证记录，不修改固件代码、Kconfig、协议实现、音频链路、VAD、KWS、tensor dump、alignment replay、board/local parity、AEC/BF 或 MCP；无需 SDK rebuild。
- `Step H.xiaozhi-client.46` 收敛 Orvibo TTS 起播回归并恢复已验证的 AudioTrack 安全约束：
  - 对照 `git` 历史和 2026-05-09 11:10 板端日志确认，当前“能连上服务器但听不到 TTS”的回归最像 `ec5bc1e` 之后的播放策略倒退，而不是协议或服务端问题。
  - Orvibo TTS 现在重新固定为 `disable_track_reuse = true`，并把 `RIVER_ORVIBO_TTS_BUFFER_FRAMES` 从 `24` 收回到 `16`，恢复历史上已验证可听的起播约束。
  - 播放服务在 `AudioTrack_Start()` / restart / start_stream()` 前显式解除 playback 与 amplifier mute，但不再接管 MCP 的硬件音量所有权。
  - 新增 `playback write failed` 诊断日志，会打印 stream、bytes、state、started、deferred、prefetched/threshold 和 track 状态，便于下一轮板端若仍无声时快速判断是否卡在 AudioTrack。
  - 保留 `SERVER_TTS_STARTED/FINISHED` 与下行音频同队列串行、`bp_evt` 只作诊断、`RIVER_ORVIBO_APP_TTS_DRAIN_MS=3000` 等已验证修复，不回到 deferred-write 路径。
  - 已完成静态检查、harness 检查和 `/root/ameba-rtos` 完整 build；当前待验证项仍是重新烧录后确认 `playback start ... reuse=no deferred=no`、无即时 `underrun`，并恢复可听 TTS。
- `Step H.xiaozhi-client.43` aligns speaking-mode uplink with the XiaoZhi realtime/barge-in contract:
  - static review against `~/xiaozhi-esp32` and `~/py-xiaozhi` shows the reference realtime path keeps audio processing alive during TTS only when the active profile is AEC/native-reference capable.
  - Orvibo previously only encoded uplink in `LISTENING`, which meant realtime-capable profiles could stall uplink while TTS was still playing.
  - `river_orvibo_audio_service` now allows uplink encoding during `SPEAKING` only when barge-in is enabled and the active local voice profile exposes `AEC` or `NATIVE_CAPTURE_REF`, keeping auto-stop profiles closed.
  - local VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, AEC/BF implementation, Opus framing, and the TTS drain/high-water fixes remain unchanged.
  - latest `/root/ameba-rtos` build passed after the change; board runtime confirmation still needs a realtime-capable TTS interruption test to verify barge-in audio continues to flow before TTS stop.
- `Step H.xiaozhi-client.44` 收敛会话切换时的残留队列：
  - Orvibo protocol close/open now explicitly clears residual uplink queue frames and records `uplink_queue_flushes`, so old audio captured before a transport teardown cannot leak into a new websocket session.
  - Orvibo app now clears queued audio work on channel loss / recoverable error / fatal error and records `audio_queue_flushes`, so stale TTS or downlink packets do not survive a session boundary and re-enter the next state machine epoch.
  - This is a deterministic reconnect-hygiene improvement, not a protocol or codec change. The known risk is that audio already queued during teardown will be discarded, but that audio is no longer trustworthy once the transport is gone.
  - latest `/root/ameba-rtos` build passed after the change; board runtime confirmation still needs a forced close/reopen stress run to verify stale frames do not reappear after reconnect.
- `Step H.xiaozhi-client.42` finishes root plan cleanup:
  - root `plan.md` no longer embeds the stale 2026-04-01 `refactor` runtime-optimization plan as if it were current branch context.
  - the old root plan body is archived under `doc/history/refactor_legacy/ARCHIVED_RUNTIME_PERFORMANCE_OPTIMIZATION_PLAN_2026-04-01_ZH.md`.
  - current planning entry points remain `.codex/active_context.md`, `.codex/active_plans.md`, and `doc/ORVIBO_CLIENT_REARCH_EXECUTION_PLAN_ZH.md`.
  - no firmware code, Kconfig, protocol, audio path, VAD, KWS, model, tensor dump, alignment replay, board/local parity, AEC/BF, MCP, or runtime behavior changed.
  - docs-only hygiene checks passed; no firmware rebuild is required for this documentation-only slice.
- `Step H.xiaozhi-client.41` hardens Orvibo TTS downlink playback completion:
  - reference review against `~/xiaozhi-esp32-server` confirmed server-side TTS stop is emitted only after the audio rate-controller queue is drained plus an additional pre-buffer playback wait.
  - local playback high-water now records `bp_evt` diagnostics instead of dropping the downlink packet before `river_playback_service_write(...)`.
  - TTS playback buffer frame budget is increased from `16` to `24`, and the high-water diagnostic threshold is raised from `85%` to `95%`.
  - app-level TTS drain timeout is increased from `900ms` to `3000ms`, reducing the chance of forcing playback stop before the local buffer has drained.
  - local VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, AEC/BF, WebSocket auth/hello/listen/abort, MCP volume-only, and Opus wire framing remain unchanged.
  - latest `/root/ameba-rtos` harness and full build passed; board runtime confirmation still requires flashing and replaying a long TTS response to verify tail completion.
- `Step H.xiaozhi-client.40` archives stale branch-era documents and tightens the active document index:
  - `doc/README.md` now prioritizes the Orvibo mainline plan plus protected KWS/VAD/AEC/BF diagnostic references, rather than old XiaoZhi/direct-provider/refactor plans.
  - `.codex/active_plans.md` now lists only `doc/ORVIBO_CLIENT_REARCH_EXECUTION_PLAN_ZH.md` as active; old `agent-server-v2`, direct-XiaoZhi, refactor, Iflytek/provider, and branch snapshot documents are reference-only under `doc/history/`.
  - stale `.codex` ASR-first / old-XiaoZhi / Iflytek process notes were moved into history so current Codex entry points no longer present them as live context.
  - no firmware code, Kconfig, protocol, audio path, VAD, KWS, model, tensor dump, alignment replay, board/local parity, AEC/BF, or MCP behavior changed.
- `Step H.xiaozhi-client.39` adds a runtime `kws config` line to `river kws status`:
  - this keeps the Step 38 conservative KWS trigger behavior unchanged.
  - board-side status now exposes `threshold_q15`, `threshold_pm`, `hold`, `cooldown_ms`, `fallback`, `weak_q15`, `weak_pm`, stride, pre-roll, queue, and log period without relying on the boot-time `kws backend` line.
  - with the current conservative profile, `river kws status` should report `threshold_q15=9831 threshold_pm=300 hold=2 cooldown_ms=2500 fallback=off weak_q15=0 weak_pm=0`.
  - local VAD, wake-word model, KWS feature extraction/inference, tensor dump, alignment replay, board/local parity, and AEC/BF implementation remain unchanged.
- `Step H.xiaozhi-client.38` applies a conservative KWS wake trigger profile based on the latest board logs:
  - the real wake example reached about `318pm/q15=10452`, while the false wake example was a single observed spike to about `338pm/q15=11107` after a prior below-threshold `283pm`.
  - the previous deployed profile used about `290pm` (`CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=9517`) with `CONFIG_RIVER_KWS_TRIGGER_HOLD_FRAMES=1`, so one high inference was sufficient to emit `wakeword hit`.
  - the new profile raises the main threshold only modestly to `9831` (about `300pm`) to avoid immediately losing the observed `318pm` true wake, but requires `hold=2` consecutive main-threshold hits and extends cooldown to `2500ms`.
  - `CONFIG_RIVER_KWS_GATE_FALLBACK_EN` is introduced so the VAD gate-close fallback can be disabled during false-trigger triage; this branch sets it to `n` so a single gate-local peak cannot bypass `hold=2`.
  - KWS startup/status/trigger/gate-close logs now expose `fallback=on|off`; with fallback disabled, `weak_pm=0` confirms the conservative profile is active on-device.
  - local VAD, wake-word model, KWS feature extraction/inference, tensor dump, alignment replay, board/local parity, and AEC/BF implementation remain unchanged.
  - latest `/root/ameba-rtos` harness and full build passed; generated config confirms `# CONFIG_RIVER_KWS_GATE_FALLBACK_EN is not set`.
  - board runtime confirmation still requires reflashing and comparing true-wake vs non-wake utterances.
- `Step H.xiaozhi-client.37` fixes a local TTS tail truncation race in Orvibo app queue ordering:
  - board log review showed a plausible symptom cluster around `tts sentence_end`, active playback/AEC reference, and an incomplete TTS tail.
  - static audit confirmed that protocol `tts stop` events were previously posted to `control_queue`, while downlink TTS audio packets were posted to `audio_queue`.
  - because the Orvibo app loop always drains `control_queue` before `audio_queue`, `tts stop` could overtake already-arrived trailing downlink audio from the same websocket stream, switch state away from `speaking`, and cause the remaining packets to be dropped as `outside speaking`.
  - `river_orvibo_app_msg_is_audio(...)` now classifies `SERVER_TTS_STARTED` and `SERVER_TTS_FINISHED` as audio-ordered messages, so they stay serialized with downlink audio in `audio_queue` and preserve stream order.
  - latest `/root/ameba-rtos` harness and full build are the current top-of-tree verification target; board runtime confirmation still requires reflashing and replaying a long TTS response.
  - local VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, and AEC/BF implementation remain unchanged.
- `Step H.xiaozhi-client.36` adds qualitative wakeword diagnostics to separate KWS model/config false triggers from Orvibo refactor-side duplicate wake handling:
  - static review still indicates the current refactor preserved the KWS input path from capture/preproc through VAD gating into `river_voice_kws_submit_frame(...)`, so there is no direct code-level evidence yet that the Orvibo rebuild broke inference input plumbing.
  - the currently deployed KWS config remains notably aggressive for an experimental branch: `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=9517` and `CONFIG_RIVER_KWS_TRIGGER_HOLD_FRAMES=1`, making single-frame crossings materially easier to trigger than the more conservative internal defaults in `river_voice_kws.cc`.
  - `wakeword hit:` now logs threshold, fallback threshold, best gate score, per-gate inference count, hit streak context, and whether the trigger came from normal threshold mode or gate-fallback mode.
  - `kws gate close:` now logs the gate-best score together with threshold/fallback thresholds and whether that gate had already latched a trigger.
  - the Orvibo audio-service bridge now logs `wake bridge: ... mode=... vad_speech=... vad_prob=...` when a KWS wake event is actually handed upward, making it easier to prove whether repeated perceived wakes come from repeated KWS triggers or from higher-level event reuse.
  - latest `/root/ameba-rtos` harness and full build are the current top-of-tree verification target; board runtime confirmation still requires fresh logs after flashing.
  - local VAD, wake-word/KWS inference implementation, tensor dump, alignment replay, board/local parity, and AEC/BF implementation remain unchanged.
- `Step H.xiaozhi-client.35` aligns Orvibo volume-only MCP failure behavior with the XiaoZhi reference stack:
  - reference comparison against `~/xiaozhi-esp32` confirmed invalid tool name / missing argument / out-of-range integer paths are returned as JSON-RPC `error.message`, not as a successful `result` envelope carrying `isError=true`.
  - reference comparison against `~/py-xiaozhi` and `~/xiaozhi-esp32-server` confirmed the current server-side device-MCP caller prefers JSON-RPC error handling for these request-shape failures and otherwise only inspects `result.content[0].text` on success.
  - the current Orvibo branch previously accepted negative volume, cast it through `uint8_t`, and could therefore wrap `-1` into a large local volume value before the later `>100` clamp.
  - Orvibo MCP now validates `self.audio_speaker.set_volume` in signed integer space before casting, rejects `<0` / `>100`, returns JSON-RPC errors for invalid tool-call shape failures, and still keeps the successful volume-only MCP surface unchanged.
  - protocol text handling now explicitly tolerates optional application-layer `pong` messages without affecting state or producing noisy logs.
  - latest `/root/ameba-rtos` harness and full build are the current top-of-tree verification target; board runtime confirmation remains blocked by the current historical `/dev/ttyUSB0` pure-`0x00` session state.
  - local VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, and AEC/BF implementation remain unchanged.
- `Step H.xiaozhi-client.34` refreshes OTA-issued websocket config before treating reconnect failure as terminal:
  - reference audit against `~/xiaozhi-esp32-server` confirmed OTA issues websocket tokens signed from `client_id|device_id|timestamp`, while the websocket server verifies both signature and `expire_seconds`.
  - the current Orvibo branch previously kept `access.ready=true` after a successful OTA pull and therefore kept retrying stale websocket credentials if a later reconnect happened after token expiry.
  - app open-audio-channel handling now checks whether the active websocket config came from OTA; on the first open failure under that condition, it refreshes access once and immediately retries the websocket open.
  - static fallback websocket deployments are intentionally excluded from this automatic refresh-retry path.
  - local VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, and AEC/BF implementation remain unchanged.
- `Step H.xiaozhi-client.33` fast-fails invalid Orvibo server hello transport negotiation:
  - protocol audit found that if the server hello message omitted `transport` or returned a non-`websocket` value, `river_orvibo_parse_server_hello()` emitted the expected protocol error but `river_orvibo_wait_server_hello()` still waited until the hello timeout unless the socket closed first.
  - this made a clearly incompatible server hello look like a generic timeout from the open-audio-channel path and kept failure latency worse than the XiaoZhi reference clients.
  - protocol context now tracks `server_hello_rejected`; invalid hello transport marks that flag before emitting the existing `hello_transport_invalid` error event.
  - `river_orvibo_wait_server_hello()` now returns `RIVER_ERR_IO` immediately when the flag is observed, and each new audio-channel open resets both `server_hello_received` and `server_hello_rejected`.
  - local VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, and AEC/BF implementation remain unchanged.
- `Step H.xiaozhi-client.32` closes the local websocket context immediately after remote-close state convergence:
  - protocol audit found that `river_orvibo_ws_close_cb()` already marks the channel closed and emits `AUDIO_CHANNEL_CLOSED`, but the state machine only transitioned back to `idle` without also calling `river_orvibo_protocol_close_audio_channel()`.
  - this left a closed `wsclient` context resident until the next explicit reconnect attempt, adding avoidable transport residue and empty polling noise after server-side close paths.
  - `CONNECTING` / `LISTENING` / `SPEAKING` / `RECOVERING` now all include `RIVER_ORVIBO_ACTION_CLOSE_AUDIO_CHANNEL` when handling `AUDIO_CHANNEL_CLOSED`, so remote-close and timeout-close paths synchronously release the local websocket context.
  - local VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, and AEC/BF implementation remain unchanged.
- `Step H.xiaozhi-client.31` closes the websocket context immediately when hello send fails:
  - protocol audit found that after `ws_connect_url()` succeeds, a `river_orvibo_send_hello()` failure returned immediately without closing the newly opened websocket context.
  - this left cleanup to the higher-level recovery path and created a short half-open session window with already-incremented session counters.
  - the `hello_send_failed` branch in `river_orvibo_protocol_open_audio_channel()` now calls `river_orvibo_close_context()` before returning, matching the existing server-hello timeout/failure cleanup path.
  - local VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, and AEC/BF implementation remain unchanged.
- `Step H.xiaozhi-client.30` fixes Orvibo TTS downlink channel propagation:
  - protocol already parsed `audio_params.channels` from server hello, but the app downlink message path forced `channels=1U` before calling the audio service.
  - this could open the Opus decoder with the wrong channel count if a XiaoZhi-compatible server is configured for non-mono TTS, even though the current default server config is `24000Hz/1ch/60ms`.
  - `river_orvibo_protocol_event_t` now carries `channels`; server hello and binary audio events pass `server_channels` through to the app.
  - the app forwards `event->channels` to `river_orvibo_audio_service_handle_downlink()` and only falls back to `1` when the field is missing.
  - the audio service now accepts `1ch/2ch` downlink Opus; `2ch` is explicitly downmixed to mono before the existing resample, stereo playback expansion, and AEC reference export path.
  - audio diag/status now reports `rate=server_rate/server_channels->playback_rate`.
  - local VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, and AEC/BF implementation remain unchanged.
- `Step H.xiaozhi-client.29` completes the optional XiaoZhi-compatible v2 activation HMAC path without changing the default activation baseline:
  - reference comparison against `~/xiaozhi-esp32` confirmed that serial-bearing clients send `Activation-Version: 2`, `Serial-Number`, and a root JSON activation payload containing `algorithm`, `serial_number`, `challenge`, and `hmac`.
  - reference comparison against `~/py-xiaozhi` confirmed HMAC-SHA256 uses the configured raw string key over the challenge and emits lowercase hex.
  - reference comparison against `~/xiaozhi-esp32-server` confirmed the current manager-api `/ota/activate` path still succeeds/fails from `Device-Id` binding state rather than v2 payload verification, so Orvibo keeps the already verified v1/no-serial flow as the default.
  - `CONFIG_RIVER_ORVIBO_ACTIVATION_SERIAL_NUMBER` and `CONFIG_RIVER_ORVIBO_ACTIVATION_HMAC_KEY` are optional; both must be set before Orvibo switches activation requests to v2/HMAC.
  - access status/logs now expose activation version, HMAC configured state, and serial number, but never expose the HMAC key.
  - local VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, and AEC/BF implementation remain unchanged.
- `Step H.xiaozhi-client.28` tightens Orvibo websocket protocol-version input:
  - reference comparison against `~/xiaozhi-esp32-server` confirmed direct websocket binary messages are treated as raw Opus unless the connection is explicitly from the MQTT gateway path.
  - the current server OTA response emits websocket `url/token` and does not emit `websocket.version`, so Orvibo should stay on the raw/v1 default unless a supported version is explicitly configured.
  - OTA websocket version parsing now accepts only `1..3`; unsupported numeric versions are logged and ignored.
  - `river_orvibo_protocol_set_config()` also rejects unsupported versions before mutating global config, preventing header/hello version from diverging from implemented audio framing.
  - local VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, and AEC/BF implementation remain unchanged.
- `Step H.xiaozhi-client.27` aligns the default Orvibo WebSocket handshake with XiaoZhi reference clients:
  - reference comparison against `~/xiaozhi-esp32` and `~/py-xiaozhi` confirmed they set auth/protocol/device headers but do not request `Sec-WebSocket-Protocol`.
  - reference comparison against `~/xiaozhi-esp32-server` confirmed the current server does not configure required websocket subprotocols.
  - the Ameba SDK would otherwise inject `Sec-WebSocket-Protocol: chat, superchat` whenever no protocol is explicitly configured.
  - `RIVER_ORVIBO_WS_SUBPROTOCOL` now defaults to an empty string, and a project-side `--wrap=ws_client_handshake` shim omits `Sec-WebSocket-Protocol` when the configured subprotocol is empty.
  - explicitly configured subprotocols remain supported through the existing Orvibo config path; connect/status logs show `ws_subprotocol=-` for the default no-subprotocol state.
  - `build.ninja` and AP image symbol checks confirm `river_ws_handshake.o`, `-Wl,--wrap=ws_client_handshake`, and `__wrap_ws_client_handshake` are present; latest-SDK build passed on `/root/ameba-rtos`.
  - board runtime confirmation remains blocked by the current historical `/dev/ttyUSB0` pure-`0x00` session state.
  - local VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, and AEC/BF implementation remain unchanged.
- `Step H.xiaozhi-client.26` corrects Orvibo client hello `features.aec` semantics:
  - reference comparison against `~/xiaozhi-esp32` confirmed WebSocket/MQTT hello only sends `features.aec=true` under `CONFIG_USE_SERVER_AEC`.
  - `xiaozhi-esp32` rejects simultaneous device-side AEC and server-side AEC at compile time, so this field represents a server-AEC request rather than local device AEC/BF/native-ref capability.
  - Orvibo hello no longer derives `features.aec` from the active local voice profile; logs now state `server_aec=no`.
  - `listen_start.mode` remains selected from the local voice-profile capability bits, preserving the H.21 listening-mode fix without misreporting local AEC as server AEC.
  - static checks, harness check, and latest-SDK build have all passed on `/root/ameba-rtos`; board runtime confirmation remains blocked by the current historical `/dev/ttyUSB0` pure-`0x00` session state.
  - local VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, and AEC/BF implementation remain unchanged.
- `Step H.xiaozhi-client.25` converges Orvibo OTA/MCP self-description metadata onto a single source:
  - reference comparison against `~/xiaozhi-esp32`, `~/py-xiaozhi`, and `~/xiaozhi-esp32-server` confirmed the OTA handler prefers request headers such as `device-model` and `application-version`, then falls back to body `board.type` / `application.version`.
  - the current Orvibo branch previously still exported scattered placeholder values such as `application.version=0.1.0`, `board.type=wifi`, and MCP `serverInfo.version=0.1.0`, which could mislead OTA model/version matching and long-term server-side device profiling.
  - a new Orvibo-owned build metadata helper now provides a single source for `app name/version`, `compile_time`, `board name/type`, `chip model`, and `User-Agent`.
  - OTA requests now export the same metadata in both headers and JSON body, including the server-preferred header keys `Device-Model`, `Application-Version`, `Firmware-Version`, and related aliases.
  - MCP initialize now reports the same Orvibo app name/version instead of a stale hard-coded version string.
  - static checks have passed; harness/build verification is the active top-of-tree target on `/root/ameba-rtos`; board runtime confirmation remains blocked by the current historical `/dev/ttyUSB0` pure-`0x00` session state.
  - local VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, and AEC/BF implementation remain unchanged.
- `Step H.xiaozhi-client.24` aligns static fallback websocket readiness with XiaoZhi-compatible unauthenticated server deployments:
  - reference comparison against `~/xiaozhi-esp32-server` confirmed the server only enforces `Authorization` when `auth.enabled=true`; auth-disabled local deployments legally accept an empty websocket token.
  - the current Orvibo branch previously treated static fallback as usable only when both `ws_url` and `ws_token` were non-empty, which incorrectly blocked explicit local websocket deployments that intentionally disable auth.
  - the fallback websocket URL default is now empty, so the branch no longer silently directs to the old hardcoded endpoint when OTA/config is absent.
  - explicit fallback `ws_url` is now enough to mark static websocket config usable; token remains optional and is only needed for auth-enabled targets.
  - static checks, harness check, and latest-SDK build have all passed on `/root/ameba-rtos`; board runtime confirmation remains blocked by the current historical `/dev/ttyUSB0` pure-`0x00` session state.
  - local VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, and AEC/BF implementation remain unchanged.
- `Step H.xiaozhi-client.23` aligns the default Orvibo websocket protocol version with the XiaoZhi baseline:
  - reference comparison across `~/xiaozhi-esp32`, `~/py-xiaozhi`, and `~/xiaozhi-esp32-server` confirmed that the live XiaoZhi websocket baseline defaults to protocol version `1`.
  - both the ESP32 and Python reference clients send websocket binary audio as raw Opus packets under version `1`, while the local Python server websocket path forwards inbound bytes directly into Opus/VAD handling without a visible v2/v3 unwrap stage.
  - the current Orvibo branch previously defaulted to protocol version `3`, which risks wrapping uplink Opus in a v3 binary envelope when OTA/config does not explicitly override `websocket.version`.
  - the Orvibo default is now version `1`, so direct websocket interop falls back to raw-Opus framing by default; OTA/config can still override the version if a deployment explicitly returns `2` or `3`.
  - static checks, harness check, and latest-SDK build have all passed on `/root/ameba-rtos`.
  - board runtime confirmation is still blocked by the current historical `/dev/ttyUSB0` pure-`0x00` session state, so raw-Opus first-turn runtime proof is not yet captured on board.
  - local VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, and AEC/BF implementation remain unchanged.
- `Step H.xiaozhi-client.22` normalizes cloud-facing wake text for XiaoZhi-compatible servers:
  - reference comparison confirmed the current branch preserves a local KWS hit text `小欧管家`, while the target server default wakeup-word handling expects `listen detect.text` values aligned with configured wake words such as `你好小智`.
  - Orvibo app now decouples the protected local KWS text from the cloud-facing `listen detect.text`, using the new configurable `RIVER_ORVIBO_SERVER_WAKE_TEXT` and defaulting it to `你好小智`.
  - static checks, harness check, and latest-SDK build have all passed on `/root/ameba-rtos`.
  - board runtime confirmation is still blocked by the current historical `/dev/ttyUSB0` pure-`0x00` session state, so the expected `server wake detect text` log is not yet captured on board.
  - local VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, and AEC/BF implementation remain unchanged.
- `Step H.xiaozhi-client.21` aligns client hello/listen-mode semantics with the active Orvibo voice profile:
  - reference comparison confirmed XiaoZhi clients do not hardcode post-wake listening mode; they switch between `auto` and `realtime` based on duplex/AEC capability.
  - Orvibo app now selects `listen_start.mode` from current voice-profile capability bits instead of always sending `auto`.
  - the H.21 attempt to map local AEC/native capture-reference capability into hello `features.aec=true` has been superseded by H.26 because the reference protocol uses that field for server-side AEC requests.
  - static checks, harness check, latest-SDK build, and reflash have all passed on `/root/ameba-rtos`.
  - board runtime confirmation is still blocked by the current historical `/dev/ttyUSB0` pure-`0x00` session state, so the expected `client hello features` / `listen start mode` logs are not yet captured on board.
  - VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, AEC/BF implementation, Opus framing, and MCP volume-only logic are unchanged.

- `Step H.xiaozhi-client.20` is verified on board after H.19 board validation exposed a TTS/close race:
  - reference behavior checked against `~/xiaozhi-esp32`, `~/py-xiaozhi`, and `~/xiaozhi-esp32-server`.
  - XiaoZhi-compatible servers may close the WebSocket after TTS in `close_after_chat` paths; the client must not treat a closed channel as a protocol-control failure while trying to send another `listen_start`.
  - Orvibo app now guards wake/listen/abort control frames with `river_orvibo_protocol_audio_channel_open()`.
  - if the channel closes before or during control-frame send, Orvibo records `protocol_ctrl skip`, posts `AUDIO_CHANNEL_CLOSED` for state convergence, and avoids recoverable-error escalation.
  - `/dev/ttyUSB0` flash passed; monitor confirmed server hello, TTS playback, close-race skip log, `protocol_ctrl=3/0 skip=1`, and convergence to `idle` without the old `listen_start:-4` recoverable-error path.
  - VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, AEC/BF, audio codec, and MCP volume-only logic are unchanged.

- `Step H.xiaozhi-client.19` fixes the real-board Orvibo audio task stack overflow:
  - `/dev/ttyUSB0` flash passed with `/root/ameba-rtos`.
  - board monitor confirmed real MAC identity, WebSocket connection, and server hello from `wss://api.tenclass.net/xiaozhi/v1/`.
  - the first wake/listening path then hit `STACK OVERFLOW - TaskName(orvibo_audio)`.
  - `orvibo_audio` task stack is increased from 18 KB to 32 KB and logs now expose `task_stack=`.
  - rebuilt with `/root/ameba-rtos`, reflashed successfully, and validated `mode=listening` with increasing `enc=` counters without another `STACK OVERFLOW`.
  - `river orvibo status` confirmed `task_stack=32768`, real `device_id=8c:bd:37:49:a6:3c`, OTA-derived WebSocket URL, MCP volume-only tools, 24 kHz server audio, and 24 kHz to 48 kHz playback adaptation.
  - follow-up runtime issue observed for a later step: after one server TTS, the WebSocket closed and the app recovered to `idle` instead of re-establishing the next listening channel.

- `Step H.xiaozhi-client.18` aligns the Orvibo runtime with XiaoZhi-compatible server text semantics:
  - `tts sentence_start`、`stt`、`llm` 现在都会进入 Orvibo protocol/app 事件流，而不是只打日志。
  - protocol status 记录最近一次 server text payload，以及 `tts_sentence_rx` / `stt_rx` / `llm_rx` 计数。
  - app status 记录 `last_server_text_kind` / `last_server_text` / `last_server_text_detail`，便于板端直接核对文本与情绪。
  - 未触碰 VAD、KWS、AEC/BF、MCP volume-only、OTA/WS 连接和 TTS 复入边界。
  - 最新 `/root/ameba-rtos` SDK build 已通过。

- `Step H.xiaozhi-client.17` aligns executable project SDK defaults with the branch baseline:
  - `components/river_cloud/CMakeLists.txt`, `tools/river_flash.py`, `tools/generate_rdev.py`, and `env.bat` now default to `/root/ameba-rtos` when `AMEBA_SDK_ROOT` is not explicitly set.
  - `tools/diag/check_codex_harness.py` now checks those active defaults so future Orvibo/XiaoZhi-compatible work cannot silently fall back to `/root/ameba-rtos-1.2`.
  - No protocol, VAD, KWS, KWS parity, AEC, BF, or audio runtime behavior changed.
- `Step H.xiaozhi-client.16` hardens Orvibo access identity before XiaoZhi-compatible auth:
  - access `ready` now requires both websocket config and a valid refreshed STA MAC identity.
  - `river_orvibo_access_refresh()` refreshes Device-Id/Client-Id from the runtime STA MAC before OTA/config.
  - unavailable or invalid STA MAC records `sta_mac_unavailable` and blocks access refresh instead of using an all-zero identity for OTA/WebSocket auth.
  - access status reports `identity=ready|waiting_mac`.
  - VAD, KWS, KWS parity tooling, AEC, and BF paths are unchanged.
- `Step H.xiaozhi-client.15` adds XiaoZhi-compatible inbound channel timeout handling:
  - protocol tracks the last inbound WebSocket message timestamp.
  - `river_orvibo_protocol_poll()` closes channels that have no inbound messages for 120 seconds and emits `AUDIO_CHANNEL_CLOSED`.
  - protocol status reports `timeout` and `incoming_age=age/limit`.
  - VAD, KWS, KWS parity tooling, AEC, and BF paths are unchanged.
- `Step H.xiaozhi-client.14` aligns Orvibo listening re-entry and abort semantics with the XiaoZhi-compatible reference:
  - `tts_stop` now drains playback, returns to listening, and sends a fresh `listen start` for the next turn.
  - VAD speech-start barge-in sends generic `abort` without a reason, then re-enters listening.
  - wake-word barge-in uses a separate `abort_wake_word` action and sends `reason=wake_word_detected`.
  - speaking + barge-in enabled opens the existing KWS detection gate so current wake-word implementation can participate during TTS.
  - VAD, KWS model/thresholds/parity tooling, AEC, and BF implementations are unchanged.
- `Step H.xiaozhi-client.13` adapts XiaoZhi-compatible TTS downlink audio to an Ameba playback-supported rate:
  - server Opus is still decoded with the sample rate and frame duration from server hello.
  - decoded mono PCM is converted to the selected playback rate before stereo expansion and reference export.
  - 24 kHz server TTS is routed to 48 kHz playback because the current Ameba output policy does not list 24 kHz.
  - audio diag/status exposes `rs=converted/bypass/fail` and `rate=server->playback`.
  - VAD, KWS, KWS parity tooling, AEC, and BF paths are unchanged.
- `Step H.xiaozhi-client.12` first removed dependence on the SDK's implicit `chat, superchat` fallback by adding an explicit Orvibo subprotocol config and diagnostics; this has been superseded by H.27:
  - the default is now no websocket subprotocol, matching XiaoZhi reference clients.
  - the Orvibo config path still supports an explicitly configured `RIVER_ORVIBO_WS_SUBPROTOCOL`.
  - protocol connect/status logs show `ws_subprotocol=-` for the default no-subprotocol state.
- `Step H.xiaozhi-client.11` expands the Orvibo Opus payload envelope for the current XiaoZhi-compatible server contract:
  - host-side OTA/WebSocket probing reached `wss://api.tenclass.net/xiaozhi/v1/` and received server hello with `opus/24000Hz/1ch/60ms`.
  - protocol binary payload, app audio message payload, and audio-service Opus packet limits are now `1536U`.
  - oversized uplink/downlink packets are counted and logged; status exposes `payload_max`, `audio_max`, `packet_max`, and `oversize=up/down`.
- `Step H.xiaozhi-client.10` adds an app-owned manual access refresh path for board-side binding validation:
  - `river orvibo refresh` posts a control message to the Orvibo app thread.
  - app handling calls the existing access refresh path with reason `diag_refresh`.
  - this allows OTA/config and activation polling to be refreshed immediately after server-side binding without rebooting or waiting for the periodic refresh.
- `Step H.xiaozhi-client.9` hardens Orvibo TTS downlink playback backpressure:
  - TTS playback buffer uses 16 frames to absorb common bursty downlink.
  - each decoded downlink frame checks playback SDK buffer occupancy before writing.
  - frames that would exceed 85% high-water are dropped with `bp_drop` diagnostics instead of blocking the app task.
  - audio status reports backpressure drops, high-water, and playback buffer occupancy.
- `Step H.xiaozhi-client.8` backs off failed Orvibo audio-channel opens:
  - failed `open_audio_channel` schedules capped exponential backoff and keeps one pending wake retry.
  - backoff suppresses immediate repeated open attempts and returns the app through recoverable recovery.
  - retry only posts from `idle` when Wi-Fi is connected and access is ready.
  - successful open or Wi-Fi loss clears the pending retry/backoff state.
- `Step H.xiaozhi-client.7` serializes Orvibo WebSocket poll/send access:
  - protocol transport lock is a recursive mutex so `ws_poll()` callbacks can synchronously send MCP volume replies without self-deadlock.
  - hello wait and steady-state poll call `ws_poll()` through the same transport lock used by uplink sender task.
  - remote WebSocket close records `transport_closed`, increments `close_events`, and still posts `AUDIO_CHANNEL_CLOSED`.
  - protocol status reports `poll` and `close_evt` counters.
- `Step H.xiaozhi-client.6` hardens Orvibo protocol control-frame failures:
  - wake detected, listen start, listen stop, and abort speaking actions now record protocol send status.
  - automatic state-machine control failures update `last_error`, increment `protocol_control_fail`, and post recoverable error events.
  - diagnostic/manual listen and abort commands record failures without forcing app recovery.
  - app status reports `protocol_ctrl=ok/fail` for board-side validation.
- `Step H.xiaozhi-client.5` separates Orvibo app control and audio queues:
  - state/connect/listen/abort control messages no longer share capacity with uplink/downlink audio packets.
  - app task drains control first, then audio with a bounded per-tick budget.
  - audio queue uses drop-oldest on pressure, preserving control-event reachability during TTS/downlink bursts.
  - app status reports `ctl_q`, `aud_q`, control/audio posted/fail, and `aud_drop_oldest`.
- `Step H.xiaozhi-client.4` hardens uplink backpressure and diagnostics:
  - `river_orvibo_protocol_send_audio()` now queues Opus uplink frames instead of synchronously entering the SDK WebSocket send queue.
  - `orvibo_uplink` sender task owns actual `ws_sendBinary()` submission with bounded short retry.
  - each queued frame carries `session_epoch`, so close/reopen cannot leak old-session audio into a new session.
  - protocol status reports uplink task, queue depth, enqueue/drop/retry/fail counters; app status labels the app-side count as `uplink_enq`.
- `Step H.xiaozhi-client.3` hardens TTS/downlink runtime behavior against the XiaoZhi-compatible reference client:
  - server binary downlink audio is only decoded while Orvibo business state is `speaking`.
  - `tts start` resets the downlink decoder and stops stale playback before accepting a new response.
  - `tts stop` waits for a bounded playback drain before returning Orvibo audio mode to listening, then stops playback even on timeout.
  - playback status now exposes SDK buffer occupancy and drain counters for board-side validation.
- `Step H.xiaozhi-client.2` hardens first-boot access against the XiaoZhi-compatible server contract:
  - `activation.code` is treated as a user-binding prompt, not as a direct `/activate` trigger.
  - `/activate` polling is gated by `activation.challenge`, matching the reference no-serial-number flow.
  - access `ready` is now distinct from `websocket_configured`; pending activation prevents opening the audio channel.
  - Wi-Fi ready primes OTA/config immediately, and connected-but-not-ready access retries every 10 seconds so binding completion can be picked up without another wake.
- `Step H.xiaozhi-client.1` makes the Orvibo-owned mainline flash-connectable against the XiaoZhi-compatible server contract:
  - OTA/config POST uses `Activation-Version` / `Device-Id` / `Client-Id` headers and applies server websocket url/token/version.
  - activation polling supports the no-serial-number payload flow used by the reference client.
  - WebSocket open sends auth/protocol/device/client headers, sends hello, and waits for server hello before reporting success.
  - wake/listen/abort/TTS/barge-in state transitions are closed around the Orvibo state machine.
  - MCP remains volume-only (`self.get_device_status`, `self.audio_speaker.set_volume`).
- Current VAD, KWS, KWS tensor dump, alignment replay, and board/local parity paths remain preserved.
- Verification passed:
  - Orvibo/XiaoZhi-compatible protocol/access grep
  - access activation/periodic-refresh grep
  - Orvibo TTS/downlink/playback-drain grep
  - Orvibo uplink queue/session-epoch grep
  - Orvibo app control/audio queue grep
  - Orvibo protocol control failure grep
  - Orvibo WebSocket poll/send serialization grep
  - Orvibo connect retry/backoff grep
  - Orvibo TTS playback backpressure grep
  - Orvibo manual access refresh grep
  - Orvibo Opus payload envelope / oversize diagnostics grep
  - host-side OTA + WebSocket hello probe
  - Orvibo WebSocket subprotocol grep
  - Orvibo downlink playback sample-rate adapter grep
  - Orvibo listen re-entry / abort reason / speaking KWS gate grep
  - Orvibo channel timeout grep
  - Orvibo access identity grep
  - MCP volume-only grep
  - protected VAD/KWS API grep
  - `git diff --check`
  - `python3 tools/diag/check_codex_harness.py`
  - `/root/ameba-rtos` SDK build with `Build done`

## Latest Hardware-Audio Slice

- `Step H.xiaozhi-client.69` validates the PA2/PA4 route on hardware:
  - Codex flashed H.68 to `/dev/ttyUSB0` with the project NAND profile; the
    tool identified `GD5F1GM7U` NAND and completed with `Finished PASS`.
  - Serial monitor confirmed nonzero capture/preproc peaks such as `29/36`,
    `33/43`, `44/49`, and `153/154`, replacing the previous persistent
    `0/0/0` capture symptom.
  - Runtime reached VAD speech detection, KWS trigger (`hits=3 triggers=1`),
    server STT/LLM/TTS, and playback start/drain/stop.
  - `river orvibo status` confirmed `access ready=yes`, Wi-Fi connected with
    IPv4, `capture_service=running frame=1024B 16000Hz/2ch/16ms`, and
    `reads=8040 wait_to=0`.
  - PA2/PA4 + DATA1 (`DMIC3/DMIC4`) is now the verified hardware capture path;
    future work should focus on voice quality/tuning rather than DATA0-3 path
    selection.
- `Step H.xiaozhi-client.68` fixes the confirmed PA2/PA4 digital mic route:
  - User confirmed the board wires `PDM_CLK` to `PA2` and `PDM_DAT1` to `PA4`.
  - H.67 sweep showed DATA1 (`DMIC3/DMIC4`) is the only path with stable
    nonzero frames, while normal capture still reverted to DATA2
    (`DMIC5/DMIC6`) and produced long-term zero peaks.
  - Board profile now uses `pdm-2mic-pa2-pa4-data1` with `DMIC3/DMIC4`.
  - A project-owned AP Audio HAL override forces
    `AUDIO_HW_DMIC_CLK_PIN=_PA_2` and `AUDIO_HW_DMIC_DATA1_PIN=_PA_4` without
    modifying `/root/ameba-rtos`.
  - Explicit board pinmux now only touches PA2 and PA4, and H.67 boot sweep is
    disabled in `prj.conf` while remaining available behind Kconfig.
  - Verification target: confirm `audio_hal_target_img2_ap` receives the
    override header, preprocessed HAL emits PA2/PA4 pinmux, then user-run board
    validation checks for `DMIC3/DMIC4` and `clk=PA2 data1=PA4` logs.
- `Step H.xiaozhi-client.67` adds a temporary boot-time DMIC path sweep:
  - H.66 manual runtime confirmed DATA2 reaches `DMIC5/DMIC6`,
    `capture params applied: ret=0`, and SDK `set DMIC clock`, but after the
    startup transient the stable `audio diag` peak remains `0/0/0`.
  - The new sweep runs once before normal Orvibo audio capture opens and scans
    DATA0 (`DMIC1/2`), DATA1 (`DMIC3/4`), DATA2 (`DMIC5/6`), and DATA3
    (`DMIC7/8`) in separate AudioRecord open/read/close cycles.
  - Validation should compare `capture path sweep result` measured peaks and
    `nonzero` counts across all four paths while speaking near the microphones.
- `Step H.xiaozhi-client.66` scans the PA DATA2 DMIC source path:
  - H.65 DATA1 (`DMIC3/DMIC4`) produced low-level nonzero noise but still no
    useful speech energy.
  - Board profile now uses `pdm-2mic-pa-data2` with `DMIC5/DMIC6`, while keeping
    `DEVICE_IN_DMIC_REF_AMIC`, PA alternate pinmux, and the H.64
    Start-before-SetParameters order.
  - Static checks, harness check, latest-SDK build, and AP image string check
    passed.
  - Automatic NAND download reached the board but failed during
    `km0_km4_ca32_app.bin` at `addr=002d7800`, result `b'\xe2'`; runtime monitor
    did not run in Codex.
  - User manual runtime confirmed `DMIC5/DMIC6`, `capture params applied: ret=0`,
    and SDK `set DMIC clock`; after a startup transient, stable peaks remained
    `0/0/0`.
- `Step H.xiaozhi-client.65` scans the PA DATA1 DMIC source path:
  - H.64 build/download succeeded and runtime capture still produced persistent
    `0/0/0` peaks despite corrected SDK AudioRecord sequencing.
  - Board profile now uses `pdm-2mic-pa-data1` with `DMIC3/DMIC4`, while keeping
    `DEVICE_IN_DMIC_REF_AMIC`, PA alternate pinmux, and the H.64
    Start-before-SetParameters order.
  - Verification passed: static checks, harness check, latest-SDK build, image
    string check, and `/dev/ttyUSB0` NAND download all succeeded.
  - Runtime monitor showed DATA1 is not hard-zero (`peak` around `20-40` and
    preproc peak around `20`), but it is still only low-level noise and remains
    `speech=no`.
  - Next hardware-audio step should scan PA DATA2 (`DMIC5/DMIC6`) or inspect
    DMIC clock/data routing against the schematic.
- `Step H.xiaozhi-client.64` follows the 2026-05-28 18:33 board log:
  - H.63 image is running and applies DMIC1/DMIC2 plus PA alternate pinmux, but
    `AudioRecord_SetParameters` still fails with `record not created` and
    capture peaks fall to persistent `0/0/0` after an initial transient.
  - SDK examples call `AudioRecord_Init()` -> `AudioRecord_Start()` ->
    `AudioRecord_SetParameters()`, so capture now follows that order.
  - After Start, capture replays board DMIC mapping and PA pinmux, then logs
    `capture params applied: ret=... params=...` for board confirmation.
  - Verification passed: static checks, harness check, latest-SDK build, image
    string check, and `/dev/ttyUSB0` NAND download all succeeded.
  - Runtime monitor after download still showed capture/preproc peaks at
    `0/0/0` / `0`; next hardware-audio step should keep this SDK call order and
    scan DMIC source routing, starting with PA DATA1 (`DMIC3/DMIC4`).
  - Per user preference, after successful builds Codex should attempt flash and
    monitor automatically; if download fails due to board mode/serial, do not
    change code and ask the user to manually download.
- `Step H.xiaozhi-client.63` follows the 2026-05-28 18:20 board log:
  - H.62 image applied the PA alternate DMIC pinmux, but long-term peak stayed near `0/1`.
  - SDK review shows `DEVICE_IN_MIC` forces AMIC usage during `AudioRecord_Init()`, so previous post-init DMIC remapping was too late for codec/DMIC clock setup.
  - Capture now sets board DMIC mapping before init and uses `DEVICE_IN_DMIC_REF_AMIC`; post-init mapping is still retained.
  - Current test profile is `pdm-2mic-pa-data0` with `DMIC1/DMIC2` plus PA alternate pinmux; user-run validation should look for SDK `set DMIC clock` and speech peak growth.
- `Step H.xiaozhi-client.62` follows the 2026-05-28 17:29 board log:
  - H.61 image was running with `DMIC3/DMIC4` and `capture=16000Hz/2ch/16ms`, but long-term peak stayed near `0/1`.
  - This points at a hardware pinmux route mismatch rather than VAD/KWS behavior.
  - Board profile now uses `pdm-2mic-pa-data1` and applies AmebaSmart PA alternate DMIC pinmux on `PA2/PA3/PA4/PA5/PA14` before capture starts.
  - Next user-run board validation should confirm `capture dmic pinmux applied: group=pa_alt pins=PA2,PA3,PA4,PA5,PA14` and whether speech raises capture/preproc peaks.
- `Step H.xiaozhi-client.61` follows the 2026-05-28 17:17 board log:
  - H.60 image was running, but long-term `audio diag peak=` stayed at `0/1`, so the issue is PDM data/channel mapping rather than VAD/KWS scoring.
  - Board profile now uses `pdm-2mic-data1`, `AUDIO_DMIC3/DMIC4`, and current active profile is 2ch ASR mainline.
  - Next user-run board validation should confirm `capture board mics applied: usage=DMIC ch0=DMIC3 ch1=DMIC4`, `audio open ... capture=16000Hz/2ch/16ms`, and whether speech raises capture/preproc peaks.
- `Step H.xiaozhi-client.60` switches the current board audio capture profile to PDM/DMIC after parsing `doc/hard/RTL8730 4寸SCH.pdf`.
  - PDF tooling installed/available: `/root/.codex/skills/pdf`, Python `pypdf` / `fitz` / `pdfplumber` / `pytesseract`, and `tesseract`.
  - Schematic P08 `RTL8730_MIC/SPK/TH` shows `PDM_CLK` / `PDM_DAT1`, while previous firmware used AMIC1/AMIC3.
  - Board profile now logs `Orvibo-RTL8730E-PDM ... usage=DMIC primary=DMIC1 secondary=DMIC2`.
  - Capture applies board mic usage/category after `AudioRecord_Init()` because AmebaSmart resets `DEVICE_IN_MIC` opens back to AMIC during stream open.
  - Latest verification passed: `git diff --check`, `python3 tools/diag/check_codex_harness.py`, and `/root/ameba-rtos` SDK build with `Build done`.
  - Next user-run board validation: flash manually, confirm `usage=DMIC`, `set DMIC clock`, no AMIC1/3 boost-gain logs, and speech raises `audio diag peak=`.

## Next Engineering Slice

- Continue Orvibo mainline behavior hardening:
  - run harness + latest-SDK build for H.25 metadata convergence
  - board-side confirmation that OTA requests now expose the intended Orvibo model/version identity
  - board-side validation that version-1 default yields direct raw-Opus websocket interop against the target XiaoZhi-compatible server
  - OTA activation UX/log capture on real board
  - MCP volume-only end-to-end validation on server call
  - board-side multi-turn wake/listen/speak/barge-in verification

## Protected Implementation Boundaries

- Preserve current Silero VAD implementation:
  - `include/river/river_voice_detector.h`
  - `components/river_voice/river_voice_detector.c`
  - `components/river_voice/river_voice_detector_silero.cc`
  - `components/river_voice/generated/river_silero_vad_model_data.*`
- Preserve current wake-word implementation and board/local parity tooling:
  - `include/river/river_voice_kws.h`
  - `components/river_voice/river_voice_kws.cc`
  - `components/river_voice/generated/*kws*`
  - KWS tensor dump, chunk dump, alignment replay, feature/input/output comparison paths
- Preserve current local preproc/AEC/BF implementation unless a future step provides an equal or stronger replacement:
  - `components/river_voice/river_voice_preproc*.c`
  - `components/river_voice/river_voice_webrtc_aecm_adapter.*`
  - `third_party/webrtc_aecm/`

## Workflow Notes

- Future `git commit` messages in this repository should use clear Chinese descriptions by default.
- Do not edit user-owned review files unless explicitly required:
  - `TIPS.md`
  - `REVIEW.md`
  - `.codex/issues.md`
