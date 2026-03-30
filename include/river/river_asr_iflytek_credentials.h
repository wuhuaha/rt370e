/* 讯飞实时转写服务的静态凭据与默认连接参数。 */
#ifndef AMEBA_RIVER_ASR_IFLYTEK_CREDENTIALS_H
#define AMEBA_RIVER_ASR_IFLYTEK_CREDENTIALS_H

/*
 * iFlytek RTASR LLM credentials.
 *
 * Per the current official "实时语音转写大模型" documentation:
 * - appId maps to the Open Platform APPID
 * - accessKeyId maps to APIKey
 * - accessKeySecret maps to APISecret
 */
#define RIVER_IFLYTEK_RTASR_APP_ID              "596745ad"
#define RIVER_IFLYTEK_RTASR_ACCESS_KEY_ID       "b844c42f4a7aa3f776d9a95a5e015ef4"
#define RIVER_IFLYTEK_RTASR_ACCESS_KEY_SECRET   "e343debbb6bb027932b34e92f9c7cd5e"

/* Backward-compatible aliases for older code paths. */
#define RIVER_IFLYTEK_RTASR_API_KEY             RIVER_IFLYTEK_RTASR_ACCESS_KEY_ID
#define RIVER_IFLYTEK_RTASR_API_SECRET          RIVER_IFLYTEK_RTASR_ACCESS_KEY_SECRET

/* Service Configuration */
#define RIVER_IFLYTEK_RTASR_SCHEME             "ws"
#define RIVER_IFLYTEK_RTASR_HOST               "office-api-ast-dx.iflyaisol.com"
#define RIVER_IFLYTEK_RTASR_PORT               80
#define RIVER_IFLYTEK_RTASR_PATH               "/ast/communicate/v1"

/* Audio Configuration */
#define RIVER_IFLYTEK_RTASR_AUDIO_ENC          "pcm_s16le"
#define RIVER_IFLYTEK_RTASR_SAMPLERATE         "16000"
#define RIVER_IFLYTEK_RTASR_LANG               "autodialect"

#endif
