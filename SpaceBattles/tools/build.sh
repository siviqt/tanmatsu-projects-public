#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# Use an explicitly selected EIM activation script, or an already exported IDF.
if [[ -n "${SB_IDF_ACTIVATION:-}" ]]; then
    if [[ ! -f "$SB_IDF_ACTIVATION" ]]; then
        echo "Activation script not found: $SB_IDF_ACTIVATION" >&2
        exit 2
    fi
    original_path="$PATH"
    environment=$(bash "$SB_IDF_ACTIVATION" -e)
    while IFS='=' read -r key value; do
        case "$key" in
            PATH) export PATH="$value:$original_path" ;;
            IDF_*|ESP_*|OPENOCD_SCRIPTS) export "$key=$value" ;;
        esac
    done <<< "$environment"
fi
if [[ -z "${IDF_PATH:-}" || ! -f "$IDF_PATH/tools/idf.py" ]]; then
    echo "Activate ESP-IDF 6.0.2 with export.sh or set SB_IDF_ACTIVATION to an EIM activation script." >&2
    exit 2
fi
python="${IDF_PYTHON_ENV_PATH:+$IDF_PYTHON_ENV_PATH/bin/python}"
python="${python:-python3}"
fault="${SB_VALIDATION_FAULT:-none}"
case "$fault" in none|i2c|display|audio|transfer) ;; *) echo "Unknown validation fault: $fault" >&2; exit 2 ;; esac
"$python" "$IDF_PATH/tools/idf.py" -B "${SB_BUILD_DIR:-build}" \
    -D "SB_VALIDATION_FAULT=$fault" -D "SB_PERFORMANCE=${SB_PERFORMANCE:-OFF}" \
    -D "SB_NET_RADIO=${SB_NET_RADIO:-ON}" -D "SB_NET_PROFILE_HEADER=${SB_NET_PROFILE_HEADER:-}" \
    -D "SB_LOCAL_GAME=${SB_LOCAL_GAME:-ON}" -D "SB_COLOR_BITS=${SB_COLOR_BITS:-16}" build
