/* 云端 ASR Provider 注册表：按名称查找当前可用的识别后端。 */
#include <stddef.h>
#include <string.h>

#include "river_asr_provider_internal.h"

#define RIVER_CLOUD_DEFAULT_PROVIDER_NAME "iflytek_rtasr"

static const river_cloud_asr_provider_ops_t *const k_river_cloud_providers[] = {
    &g_river_cloud_iflytek_rtasr_ops,
};

const river_cloud_asr_provider_ops_t *river_cloud_provider_lookup(const char *name)
{
    size_t index;

    if (name == NULL || name[0] == '\0') {
        return NULL;
    }

    for (index = 0; index < (sizeof(k_river_cloud_providers) / sizeof(k_river_cloud_providers[0]));
         ++index) {
        const river_cloud_asr_provider_ops_t *provider;
        const char *provider_name;

        provider = k_river_cloud_providers[index];
        if (provider == NULL || provider->provider_name == NULL) {
            continue;
        }

        provider_name = provider->provider_name();
        if (provider_name != NULL && strcmp(provider_name, name) == 0) {
            return provider;
        }
    }

    return NULL;
}

const river_cloud_asr_provider_ops_t *river_cloud_provider_default(void)
{
    return river_cloud_provider_lookup(RIVER_CLOUD_DEFAULT_PROVIDER_NAME);
}
