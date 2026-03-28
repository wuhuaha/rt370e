# `/root/river-openwakeword-lab` 迁移评估报告

Date: 2026-03-27

## 1. 结论摘要

结论先行：

- **`/root/river-openwakeword-lab` 的迁移价值明显高于 `/root/kws-training-pro`。**
- **它不是一个“实验性想法仓”，而是一套已经跑通过 teacher、student、量化导出、板端编译打包链的宿主训练工作区。**
- **其中最值得迁移和复用的是：**
  - board-aligned `98x40 log-mel` 特征链
  - DS-CNN student 训练脚本
  - int8 TFLite 导出和一致性验证链
  - OpenWakeWord teacher 的数据、评估、挖掘方法
- **但它当前仍然不能被视为“已经可以直接量产部署”的完成态。**
- **主要原因不是工程链断裂，而是模型质量和中间环节还没闭环：**
  - student 仍是 `hard-label only baseline`
  - `assistant / KD` 尚未真正落地
  - teacher round6 没有完成 corrected eval 收口
  - 当前 student 最佳 round 仍然 `board_negative_eval fpr` 过高，明确 `no-deploy`

一句话判断：

- **方法成熟度：高**
- **板端对齐程度：高**
- **工程可迁移性：高**
- **当前现成模型可直接量产部署：否**

## 2. 本次评估范围

本次评估覆盖：

- 训练工作区：`/root/river-openwakeword-lab`
- 目标工程：`/root/ameba-river`

重点问题：

1. 这个工作区里的训练脚本和导出链是否已对齐当前板端 KWS 契约
2. 其中哪些模型/脚本可以直接迁入当前工程主线
3. 当前现成 round 产物是否可直接落板
4. 若继续沿这条路线推进，最合理的落地计划是什么

## 3. `/root/river-openwakeword-lab` 实际包含什么

与 `/root/kws-training-pro` 不同，这个目录已经不是单纯“teacher + 一个 student 脚本”的形态，而是一套比较完整的训练实验室，包含：

- 数据与脚本：
  - `tools/openwakeword/build_xiaou_teacher_corpus.py`
  - `tools/openwakeword/preprocess_xiaou_multisource.py`
  - `tools/openwakeword/extract_xiaou_teacher_features.py`
  - `tools/openwakeword/train_xiaou_guanjia.py`
  - `tools/openwakeword/eval_xiaou_guanjia.py`
  - `tools/openwakeword/river_kws_features.py`
  - `tools/openwakeword/extract_xiaou_student_features.py`
  - `tools/openwakeword/train_xiaou_student_dscnn.py`
  - `tools/openwakeword/export_xiaou_student_tflite.py`
- TTS 数据生成：
  - `generate_mimo_dataset.py`
  - `generate_volcengine_dataset.py`
  - `generate_iflytek_dataset.py`
- 文档：
  - `docs/OPENWAKEWORD_TRAINING_LOG_ZH.md`
  - `docs/OPENWAKEWORD_DSCNN_SYSTEM_ANALYSIS_ZH.md`
  - `docs/OPENWAKEWORD_DSCNN_ROUND56_IMPLEMENTATION_ZH.md`
  - `docs/plan.md`
- 工件：
  - round1 ~ round6 的 teacher/student 训练与评估产物
  - round5 / round6 synthetic inventory
  - round6 student int8 TFLite 导出结果

这说明它已经覆盖了：

- 数据生成
- 数据预处理
- teacher 训练与评估
- student 特征提取
- student 训练
- student 量化导出
- 板端打包前验证

工程成熟度比 `/root/kws-training-pro` 明显更高。

## 4. 与当前板端契约的对齐程度

## 4.1 student 特征链：**高度对齐**

`tools/openwakeword/river_kws_features.py` 中的 `extract()` 路径与当前板端 `components/river_voice/river_voice_kws.cc` 的特征契约高度一致。

两边关键参数一致：

- `sample_rate = 16000`
- `window_samples = 512`
- `hop_samples = 160`
- `mel_bins = 40`
- `feature_frames = 98`
- `feature_db_min = -80`
- `feature_mean = -42.1177063`
- `feature_std = 17.5219841`

而且该脚本真正采用的是：

1. power spectrum
2. 三角 mel filterbank
3. `mel_band_norm`
4. `10 * log10`
5. `max_db -> relative_db`
6. 裁剪到 `[-80, 0]`
7. 固定 mean/std 归一化

这与当前板端 `river_voice_kws.cc` 的 `river_voice_kws_compute_mel_frame()` 和 `river_voice_kws_fill_input_tensor()` 的语义是一致的。

这点非常关键，因为它说明：

- **这个工作区的 student 路线，不是“看起来像板端”，而是真正按当前板端特征合同来做的。**

这与 `/root/kws-training-pro` 最大的差异就在这里。

## 4.2 student 输入输出形状：**对齐**

`train_xiaou_student_dscnn.py` 中 student 的输入是：

- `1 x 98 x 40`

导出到 TFLite 后，`export_xiaou_student_tflite.py` 报告的输入为：

- `shape = [1, 98, 40, 1]`
- `dtype = int8`
- `scale = 0.017904678...`
- `zero_point = -7`

输出为：

- `shape = [1, 1]`
- `dtype = int8`
- `scale = 0.00390625`
- `zero_point = -128`

这与当前板端 `river_voice_kws.cc` 里：

- `input=98x40x1`
- `model_in=int8`
- `model_out=int8`

的目标形态是一致的。

## 4.3 student 导出链：**成熟**

`export_xiaou_student_tflite.py` 的导出链做了三件很重要的事：

1. 用 Keras 重建 student
2. 把 PyTorch 权重逐层映射到 Keras
3. 做 int8 TFLite 导出，并验证：
   - `PyTorch vs Keras`
   - `PyTorch vs TFLite`
   - `Keras vs TFLite`

当前 round6 targeted 导出报告显示：

- `tflite_size_bytes = 32344`
- `PyTorch vs Keras mean_abs_diff ≈ 6.56e-08`
- `PyTorch vs TFLite mean_abs_diff ≈ 0.00737`

这说明：

- 模型导出与量化数值一致性已经做得比较扎实
- 量化误差在端侧小模型里属于可接受水平

从“能否量化出一份可信 int8 TFLite”这个问题上，这个工作区的答案是：

- **可以**

## 4.4 teacher 路线：**适合作为 host teacher，不适合作为最终板端 runtime**

这个工作区里的 teacher 路线依然是 OpenWakeWord 方法：

- `extract_xiaou_teacher_features.py`
- `train_xiaou_guanjia.py`
- `eval_xiaou_guanjia.py`

它的角色定位是合理的：

- host 侧 reference teacher
- false activation mining engine
- near-miss / context 热点分析器

它并没有试图把 OWW 原样变成板端默认 runtime，这一点是正确的。

因此：

- **teacher 路线可迁，但角色只能是 host teacher/reference，不是 MCU 最终模型。**

## 5. 当前 round 产物是否可以直接迁用

## 5.1 round6 student 脚本：**可以迁**

以下脚本迁移价值很高：

- `tools/openwakeword/river_kws_features.py`
- `tools/openwakeword/extract_xiaou_student_features.py`
- `tools/openwakeword/train_xiaou_student_dscnn.py`
- `tools/openwakeword/export_xiaou_student_tflite.py`

原因：

1. 特征合同与板端一致
2. student 结构 MCU 友好
3. 训练报告口径清晰
4. 导出链可复现

如果当前工程后续要建立正式 DS-CNN 训练主线，这四个脚本是最值得吸收的部分。

## 5.2 round6 student 现成权重：**可用于工程 smoke，不可作为默认部署**

当前 round6 targeted student 最终报告：

- `best_epoch = 5`
- `recommended_threshold = 0.65`
- `board_positive_eval recall = 1.0`
- `board_negative_eval fpr = 0.7143`

round6 baseline student 报告：

- `recommended_threshold = 0.55`
- `board_positive_eval recall = 1.0`
- `board_negative_eval fpr = 0.5556`

这说明：

- hardest positive 召回已经能保住
- 但板载负例误唤醒率仍然很高

因此当前 student 权重的正确定位是：

- **工程 smoke / 量化导出 / 板端 build smoke：可用**
- **默认量产唤醒模型：不可用**

也就是说：

- **“工程上可落”不等于“效果上可部署”**

## 5.3 round5 / round6 teacher 现成权重：**不能直接作为板端模型**

round5 teacher corrected eval 给出的推荐点为：

- `threshold = 0.15`
- `clip_recall = 0.538462`
- `false_positives_per_hour = 38.96447`

并且最热负例集中在：

- `phone_wake_like_negative`
- `board_verifier_negative`

这轮 teacher 本身已经是 `no-deploy`，只能作为诊断和方法验证。

另外：

- `xiaou_teacher_round6` 目前只有训练产物，没有最终 corrected eval 报告
- 也就是说，这条线还没有真正收口

因此：

- **teacher 工件可用于继续 host 分析，不可作为当前落板依据**

## 6. 当前工作区的核心优势

## 6.1 它已经把“板端一致特征”真正落地了

这是与 `/root/kws-training-pro` 的决定性区别。

`/root/kws-training-pro` 的问题是：

- 虽然有 `40x98` 版本，但训练实际使用的特征并不严格等于当前板端前端

而 `/root/river-openwakeword-lab` 的 student 路线已经明确按板端契约实现。

这意味着：

- 这里的 student 训练结果更有资格解释板测结果

## 6.2 它已经把“训练 -> 量化 -> build”链打通了

从当前材料看，这条链已经具备：

1. 训练脚本
2. 特征提取
3. 量化导出
4. 导出一致性验证
5. 板端 header 打包接口
6. 历史上已打通过 board build

这比只停留在 ONNX 或纯 host eval 的工作区强得多。

## 6.3 它的数据治理和实验记录是规范的

这个工作区最大的优点之一，是实验记录和方法文件是体系化的：

- `OPENWAKEWORD_TRAINING_LOG_ZH.md`
- `OPENWAKEWORD_DSCNN_SYSTEM_ANALYSIS_ZH.md`
- `OPENWAKEWORD_DSCNN_ROUND56_IMPLEMENTATION_ZH.md`
- `plan.md`

这意味着：

- 它不只是“跑出过一个模型”
- 而是已经沉淀成了可追溯的工程方法

## 7. 当前工作区的关键不足

## 7.1 `assistant / KD` 仍停留在计划层，没有真正落地

文档中多次提到：

- `teacher assistant`
- `assistant soft target`
- `KD student`

但当前工作区里实际不存在这些关键脚本：

- `train_xiaou_teacher_assistant.py`
- `train_xiaou_student_verifier.py`
- `eval_xiaou_student.py`

也就是说：

- 当前 student 仍然是 **hard-label baseline**
- 还没有真正吃到 teacher 的软边界知识

这与当前 student 高召回、高误唤醒的结果是高度一致的。

## 7.2 teacher round6 没有最终 corrected eval，训练闭环还没彻底闭合

`xiaou_teacher_round6` 当前有：

- corpus
- feature files
- `.pt / .onnx / .yml`
- training log

但没有：

- `eval/board_eval_corrected_report.json`

这意味着当前 teacher round6 还没有形成最终可判断结论。

所以从流程完备性上说：

- **teacher 路线还没真正收口**

## 7.3 当前导出的 TFLite 使用了 `PAD`，而当前分支板端 resolver 还没完全匹配

round6 targeted int8 导出报告显示 op 集为：

- `CONV_2D`
- `DEPTHWISE_CONV_2D`
- `FULLY_CONNECTED`
- `LOGISTIC`
- `MEAN`
- `PAD`

但当前 `DS-CNN` 分支的 `components/river_voice/river_voice_kws.cc` resolver 只有：

- `Quantize`
- `Conv2D`
- `DepthwiseConv2D`
- `Mean`
- `FullyConnected`
- `Logistic`

没有 `AddPad()`。

这意味着：

- **这个工作区的 round6 exported model 在当前分支上并不是“无修改直接可运行”**
- 迁移时必须二选一：
  1. 板端 resolver 增加 `PAD`
  2. 导出阶段消掉 `PAD`

这是一个明确的工程兼容性点，不能忽略。

## 7.4 student 的 threshold 选择策略偏 recall-first，不等于最终产品 operating point

`train_xiaou_student_dscnn.py` 当前推荐阈值的 key 是：

1. 优先最高 board recall
2. 再看与 target FPR 的偏差
3. 最后再看 board negative FPR

这会导致：

- 推荐阈值更像“研发探索 operating point”
- 不一定是“量产最优 operating point”

例如 round6 targeted：

- `0.65` 阈值时 recall 保住，但 `board_negative_eval fpr = 0.7143`
- 更高阈值虽然 FPR 下降，但 recall 会显著掉

因此：

- 当前推荐阈值逻辑适合研发阶段，不适合直接当量产门限策略

## 8. 与 `/root/kws-training-pro` 的对比判断

如果把两个工作区放在一起比较，结论很明确：

### `/root/kws-training-pro`

优点：

- 有 teacher-student 想法
- 有 DS-CNN 架构

缺点：

- 特征链不够对齐
- 验证链不一致
- KD 实现不稳
- 更像方法原型

### `/root/river-openwakeword-lab`

优点：

- 数据、teacher、student、导出、文档都更完整
- student 特征链已基本对齐板端
- 量化与导出一致性已验证
- 有真实 round 结果和失败分析

缺点：

- 还没把 assistant / KD 真正落地
- 当前现成 round 质量仍未达到 deploy 门槛
- 当前 exported model 与本分支 resolver 还存在 `PAD` 兼容差异

最终判断：

- **如果要二选一，`/root/river-openwakeword-lab` 才是应继续推进的主线。**

## 9. 迁移建议

## 9.1 建议直接保留为“外部训练实验室”，不建议再搬回主固件仓

这个工作区已经具备：

- 大量训练工件
- 大量文档
- 多轮实验记录

把它重新塞回主固件仓没有意义，只会增加主仓复杂度。

更合理的做法是：

- **继续保持它作为外部训练实验室**
- 主仓只保留：
  - 板端 runtime
  - 模型头文件
  - 少量导出入口和引用文档

## 9.2 优先迁移“脚本能力”，不要迁移“旧结论”

最应该复用的是：

- `river_kws_features.py`
- `extract_xiaou_student_features.py`
- `train_xiaou_student_dscnn.py`
- `export_xiaou_student_tflite.py`
- `extract_xiaou_teacher_features.py`
- `eval_xiaou_guanjia.py`

最不应直接继承的是：

- “当前 round6 student 就够用了”
- “当前 teacher round5/round6 已可作为稳定 soft target”

这些结论都不成立。

## 9.3 近期最合理路线

建议按下面顺序推进：

1. 保持 `/root/river-openwakeword-lab` 作为训练主工作区
2. 以当前 board-aligned student 特征链为唯一 student 真值
3. 补齐：
   - `assistant`
   - `KD student`
   - `student verifier`
4. 解决导出模型与当前分支 resolver 的 `PAD` 兼容问题
5. 重新做 threshold / board A/B

## 10. 推荐的实施阶段

## Phase A：收 teacher round6

目标：

- 为 teacher 路线形成最终 corrected eval 结论

输出：

- `xiaou_teacher_round6/eval/board_eval_corrected_report.json`
- 最热负例清单
- 是否具备 soft target 资格的判断

## Phase B：落 assistant

目标：

- 在板端一致特征域里建立 assistant

输出：

- `train_xiaou_teacher_assistant.py`
- assistant eval report

成功标准：

- assistant 在 `near-miss / verifier-context` 上优于 hard-label baseline

## Phase C：KD student

目标：

- 把 teacher/assistant 的软边界迁给 DS-CNN student

输出：

- KD student checkpoint
- 与 hard-label baseline 的对比报告

成功标准：

- 保持 board positive recall
- 明显降低 board negative FPR

## Phase D：resolver / export 对齐

目标：

- 让 exported int8 model 与当前板端 runtime 完整匹配

动作：

- `AddPad()` 或导出图去 `PAD`
- 重新做 build smoke

## Phase E：量产门限与 verifier

目标：

- 找到真正适合产品的 operating point

要求：

- 不再只按 recall-first 选阈值
- 引入 verifier 或更合理的双级判定

## 11. 最终判断

最终建议如下：

- **`/root/river-openwakeword-lab` 应继续作为当前项目的主要唤醒训练实验室。**
- **它的迁移价值高，且明显高于 `/root/kws-training-pro`。**
- **其中 student 特征链、训练链和导出链已经足够成熟，值得直接作为当前工程的正式主线。**
- **但当前现成 round 模型仍然是“工程可落地、效果不可部署”的状态。**

因此最准确的结论是：

- **工作区可迁，方法可迁，脚本可迁，现成模型不可直接量产。**

---

## 附：本次判断依赖的关键证据文件

- `/root/river-openwakeword-lab/tools/openwakeword/README.md`
- `/root/river-openwakeword-lab/tools/openwakeword/river_kws_features.py`
- `/root/river-openwakeword-lab/tools/openwakeword/extract_xiaou_student_features.py`
- `/root/river-openwakeword-lab/tools/openwakeword/train_xiaou_student_dscnn.py`
- `/root/river-openwakeword-lab/tools/openwakeword/export_xiaou_student_tflite.py`
- `/root/river-openwakeword-lab/tools/openwakeword/extract_xiaou_teacher_features.py`
- `/root/river-openwakeword-lab/tools/openwakeword/train_xiaou_guanjia.py`
- `/root/river-openwakeword-lab/tools/openwakeword/eval_xiaou_guanjia.py`
- `/root/river-openwakeword-lab/artifacts/openwakeword/xiaou_student_round6_baseline/training/xiaou_student_round6_baseline_report.json`
- `/root/river-openwakeword-lab/artifacts/openwakeword/xiaou_student_round6_targeted/training/xiaou_student_round6_targeted_report.json`
- `/root/river-openwakeword-lab/artifacts/openwakeword/xiaou_student_round6_targeted/export/xiaou_student_round6_targeted_int8_export_report.json`
- `/root/river-openwakeword-lab/artifacts/openwakeword/xiaou_teacher_round5/eval/board_eval_corrected_report.json`
- `/root/river-openwakeword-lab/docs/OPENWAKEWORD_TRAINING_LOG_ZH.md`
- `/root/river-openwakeword-lab/docs/OPENWAKEWORD_DSCNN_SYSTEM_ANALYSIS_ZH.md`
- `/root/river-openwakeword-lab/docs/OPENWAKEWORD_DSCNN_ROUND56_IMPLEMENTATION_ZH.md`
- `/root/river-openwakeword-lab/docs/plan.md`
- `/root/ameba-river/components/river_voice/river_voice_kws.cc`
- `/root/ameba-river/KWS_PIPELINE_ZH.md`
- `/root/ameba-river/prj.conf`
