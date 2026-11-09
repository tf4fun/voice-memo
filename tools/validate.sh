#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware]" >&2
}

run_static_checks() {
    # Runtime fixtures deliberately leave unused platform references. Use the
    # native linker's dead-code option on both supported host platforms.
    local dead_code_flag="-Wl,--gc-sections"
    if [[ "$(uname -s)" == Darwin ]]; then dead_code_flag="-Wl,-dead_strip"; fi
    local actionlint_bin
    local test_dir

    python3 tools/check_repo.py
    python3 tests/test_server_launcher.py
    python3 tools/generate_memo_terminal.py --check

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    test_dir="$(mktemp -d /tmp/ai-passport-host-tests.XXXXXX)"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_pixel_math.c main/ui_pixel_math.c \
        -o "${test_dir}/test_ui_pixel_math"
    "${test_dir}/test_ui_pixel_math"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_demo_navigation.c main/demo_navigation.c \
        -o "${test_dir}/test_demo_navigation"
    "${test_dir}/test_demo_navigation"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_key_guard.c -o "${test_dir}/test_key_guard"
    "${test_dir}/test_key_guard"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_memo_state.c main/memo_state.c \
        -o "${test_dir}/test_memo_state"
    "${test_dir}/test_memo_state"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_memo_capture.c -o "${test_dir}/test_memo_capture"
    "${test_dir}/test_memo_capture"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_memo_progress.c -o "${test_dir}/test_memo_progress"
    "${test_dir}/test_memo_progress"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_memo_text.c main/memo_text.c \
        -o "${test_dir}/test_memo_text"
    "${test_dir}/test_memo_text"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_memo_time.c main/memo_time.c \
        -o "${test_dir}/test_memo_time"
    "${test_dir}/test_memo_time"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_memo_container.c main/memo_container.c \
        -o "${test_dir}/test_memo_container"
    "${test_dir}/test_memo_container"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/upload_stubs -Itests/audio_stubs -Imain \
        tests/test_memo_upload.c -o "${test_dir}/test_memo_upload"
    "${test_dir}/test_memo_upload"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/upload_stubs -Itests/audio_stubs -Imain \
        tests/test_memo_spool.c main/memo_container.c -o "${test_dir}/test_memo_spool"
    "${test_dir}/test_memo_spool"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/recorder_stubs -Itests/upload_stubs -Itests/audio_stubs -Imain \
        tests/test_memo_recorder.c main/memo_container.c -o "${test_dir}/test_memo_recorder"
    "${test_dir}/test_memo_recorder"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/upload_stubs -Itests/audio_stubs -Imain \
        tests/test_memo_events.c -o "${test_dir}/test_memo_events"
    "${test_dir}/test_memo_events"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/api_stubs -Itests/upload_stubs -Itests/audio_stubs -Imain \
        tests/test_memo_api.c main/memo_text.c -o "${test_dir}/test_memo_api"
    "${test_dir}/test_memo_api"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/discovery_stubs -Itests/upload_stubs -Itests/audio_stubs -Imain \
        tests/test_memo_discovery.c -o "${test_dir}/test_memo_discovery"
    "${test_dir}/test_memo_discovery"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_memo_sse.c -o "${test_dir}/test_memo_sse"
    "${test_dir}/test_memo_sse"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_memo_handoff.c -o "${test_dir}/test_memo_handoff"
    "${test_dir}/test_memo_handoff"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_memo_browse.c -o "${test_dir}/test_memo_browse"
    "${test_dir}/test_memo_browse"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_memo_notice.c -o "${test_dir}/test_memo_notice"
    "${test_dir}/test_memo_notice"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_memo_row_tone.c -o "${test_dir}/test_memo_row_tone"
    "${test_dir}/test_memo_row_tone"
    "${CC:-cc}" -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror -pthread \
        -Itests/network_stubs -Imain tests/test_memo_network_info.c \
        -o "${test_dir}/test_memo_network_info"
    "${test_dir}/test_memo_network_info"
    "${CC:-cc}" -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror -pthread \
        -Itests/network_stubs -Itests/upload_stubs -Itests/audio_stubs -Imain \
        tests/test_memo_net.c -o "${test_dir}/test_memo_net"
    "${test_dir}/test_memo_net"
    "${CC:-cc}" -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror -pthread \
        -Itests/provision_stubs -Itests/upload_stubs -Itests/audio_stubs -Imain \
        tests/test_memo_provision.c -o "${test_dir}/test_memo_provision"
    "${test_dir}/test_memo_provision"
    "${CC:-cc}" -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror \
        -Itests/ui_stubs -Itests/upload_stubs -Itests/audio_stubs -Imain \
        tests/test_memo_ui.c tests/ui_stubs/ui_runtime.c main/memo_ui_common.c \
        main/memo_ui_detail.c main/memo_ui_settings.c main/memo_ui_provision.c main/memo_font.c \
        main/memo_text.c main/memo_time.c -o "${test_dir}/test_memo_ui"
    "${test_dir}/test_memo_ui"
    "${CC:-cc}" -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror \
        -ffunction-sections -fdata-sections -Itests/app_stubs -Itests/ui_stubs \
        -Itests/settings_stubs -Itests/upload_stubs -Itests/audio_stubs -Imain \
        tests/test_memo_app.c tests/ui_stubs/ui_runtime.c main/memo_ui_common.c \
        main/memo_ui_detail.c main/memo_ui_settings.c main/memo_ui_provision.c \
        main/memo_font.c main/memo_state.c main/memo_settings.c main/memo_text.c \
        main/memo_time.c main/memo_container.c "${dead_code_flag}" \
        -o "${test_dir}/test_memo_app"
    "${test_dir}/test_memo_app"
    "${test_dir}/test_memo_app" wake-held
    "${test_dir}/test_memo_app" wake-released
    "${test_dir}/test_memo_app" wake-short
    "${test_dir}/test_memo_app" wake-up
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_memo_power.c -o "${test_dir}/test_memo_power"
    "${test_dir}/test_memo_power"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/settings_stubs -Itests/upload_stubs -Itests/audio_stubs -Imain \
        tests/test_memo_settings.c -o "${test_dir}/test_memo_settings"
    "${test_dir}/test_memo_settings"
    "${CC:-cc}" -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror \
        -Itests/player_stubs -Itests/upload_stubs -Itests/audio_stubs -Imain \
        tests/test_memo_player.c main/memo_container.c -o "${test_dir}/test_memo_player"
    "${test_dir}/test_memo_player"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_memo_rejection_layout.c -o "${test_dir}/test_memo_rejection_layout"
    "${test_dir}/test_memo_rejection_layout"
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_memo_rejection_font.py
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_display_rounding.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_display_rounding"
    "${test_dir}/test_bsp_display_rounding"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_es8311_sleep_check.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_es8311_sleep_check"
    "${test_dir}/test_bsp_es8311_sleep_check"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/battery_stubs -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_battery.c -o "${test_dir}/test_bsp_battery"
    "${test_dir}/test_bsp_battery"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_button.c -o "${test_dir}/test_bsp_button"
    "${test_dir}/test_bsp_button"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_lvgl_init.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_lvgl_init"
    "${test_dir}/test_bsp_lvgl_init"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/audio_stubs -Icomponents/bsp/include -Icomponents/bsp/src \
        tests/test_bsp_audio_recovery.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_audio_recovery"
    "${test_dir}/test_bsp_audio_recovery"
    for demo in audio low_power ble wifi; do
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
            -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain \
            "tests/test_demo_${demo}_runtime.c" "${dead_code_flag}" \
            -o "${test_dir}/test_demo_${demo}_runtime"
        "${test_dir}/test_demo_${demo}_runtime"
    done
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_battery_profile.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_deep_sleep_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_key_wake_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_check_repo.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_verify_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_archive_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_install_passport_skills.py
    rm -rf "${test_dir}"
    echo "Host tests: PASS"
}

run_firmware_checks() (
    local validation_build_dir

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; activate ESP-IDF 5.5.3 first." >&2
        return 1
    fi

    validation_build_dir="$(mktemp -d /tmp/ai-passport-firmware.XXXXXX)"
    trap 'case "${validation_build_dir}" in /tmp/ai-passport-firmware.*) rm -rf -- "${validation_build_dir}" ;; esac' EXIT

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults" \
        idf.py -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
    idf.py -B "${validation_build_dir}" merge-bin \
        -o "${validation_build_dir}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${validation_build_dir}"
    PYTHONDONTWRITEBYTECODE=1 python3 tools/archive_firmware.py create \
        "${validation_build_dir}" --archive-root "${repo_root}/build/firmware"
    mkdir -p "${repo_root}/build"
    install -m 0644 \
        "${validation_build_dir}/FoloToy-AI-Passport-full.bin" \
        "${repo_root}/build/FoloToy-AI-Passport-full.bin"
    echo "Firmware build: PASS"
)

cd "${repo_root}"
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac
