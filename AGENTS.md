# Repository Guidelines

## Project Structure & Module Organization
- Runtime code follows the ESP-IDF layout: entry logic in `main/` (see `main.c`) and reusable drivers/utilities in `components/` (`led_matrix`, `display_controller`, `sensors`, `wifi_config`, `demo_mode`). The `demo_mode` component handles USB host detection and kiosk animations; `wifi_config` now also accepts OTA uploads. The legacy `tap_detection/` module is parked for removal and should not receive new work.
- Vehicle safety documentation lives in `EMERGENCY_LED_SAFETY_REPORT.md` and `NEOPIXEL_IMPLEMENTATION_REPORT.md`; engineering history and decisions belong in `specs/` and `Research/`.
- Treat `build/` as generated output from `idf.py`; it should stay untracked along with transient logs. Persist configuration through `sdkconfig` (checked in) and `sdkconfig.ci`.

## Build, Flash & Monitor Commands
- Set up the toolchain once per checkout: `idf.py set-target esp32s3` (stores the choice in `sdkconfig`).
- Compile firmware with `idf.py build`; use `idf.py clean` before switching branches with divergent components.
- Flash and attach the serial monitor in one shot: `idf.py -p /dev/cu.usbmodem* flash monitor`. Use `idf.py monitor` alone when you only need logs.
- Inspect image size and partition usage via `idf.py size-components` to confirm LED control fits within safety margins.

## Coding Style & Naming Conventions
- Follow ESP-IDF C conventions: four-space indentation, braces on the same line as declarations, `snake_case` for functions/variables, and `SCREAMING_SNAKE_CASE` for constants/macros. Keep lines ≲100 characters.
- Each component exposes a public header in its `include/` folder; prefer forward declarations over massive umbrella headers. Document scheduler timing and Wi-Fi behaviours in comments instead of describing obvious control flow.
- Protect hardware limits with `assert`/`ESP_RETURN_ON_ERROR` as demonstrated in `components/sensors` and log anomalies with `ESP_LOGW`/`ESP_LOGE`.

## Testing Guidelines
- Co-locate Unity tests under `components/<module>/test/` or dedicated `test_apps/`. Name suites after the module (`test_led_matrix.c`) and gate critical paths (scheduler timing, sensor filtering, Wi-Fi triggers).
- Run them with `idf.py -T <test_app> build flash monitor` or through CI’s `idf.py unity_test`. Capture monitor output for regressions and attach traces to reviews.
- For bench validation (timed wake-ups, thermal runs), record hardware setups and measured values under `docs/` and cross-link in PRs.

## Commit & Pull Request Guidelines
- Use concise conventional commits (`feat:`, `fix:`, `docs:`); reserve `CRITICAL:` for safety hotfixes that must reach the field immediately.
- Every PR should outline risk impact, summarize validation (`idf.py build`, bench logs, scheduler interval captures), and reference any updated safety documentation.
- Include before/after imagery or serial snippets when behavior or timing changes. Never mix generated artifacts or IDE metadata with firmware changes in the same commit.
