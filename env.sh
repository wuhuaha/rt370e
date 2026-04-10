source "${AMEBA_SDK_ROOT:-/root/ameba-rtos}/env.sh"

if ! command -v ccache >/dev/null 2>&1; then
    export PATH="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/tools/shims:$PATH"
fi
