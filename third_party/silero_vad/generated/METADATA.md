# Generated Silero VAD Artifact Metadata

- source onnx: `../upstream/silero_vad_16k_op15.onnx`
- source onnx sha256: `7ed98ddbad84ccac4cd0aeb3099049280713df825c610a8ed34543318f1b2c49`
- generated artifact: `silero_vad_16k_b1_fp32.tflite`
- generated artifact sha256: `5a532943646b1dd71930fb02e26e0600ba97ee80990302726294aef8a3142a05`
- generated artifact size: `1248388` bytes
- generation date: `2026-03-11`

## Contract

- runtime: `TensorFlow Lite`
- batch: `1`
- audio input shape: `[1, 576]`
- state input shape: `[2, 1, 128]`
- probability output shape: `[1, 1]`
- next-state output shape: `[2, 1, 128]`

## Verification

- verification report: `silero_vad_16k_b1_fp32_verification.json`
- max abs diff output vs ONNX: `1.5599653124809265e-08`
- max abs diff state vs ONNX: `1.6689300537109375e-06`

## Generation Path

- reconstruction script:
  - `tools/silero_vad/rebuild_tf_silero_vad.py`
- source staging script:
  - `tools/silero_vad/stage_conversion_source.py`
- source inspection scripts:
  - `tools/silero_vad/extract_onnx_manifest.py`
  - `tools/silero_vad/extract_reconstruction_tensors.py`
