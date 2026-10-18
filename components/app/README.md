# App-only components

These components are the firmware's screen, its entry policy and its board adapters.
They are not compiled by the host build; `host/tests/layering.sh` H3 checks that they
are not in `host/Makefile` and that they do not hide a host fallback. A module belongs
here only when it cannot run on the host (LVGL panel, GPIO, Wi-Fi, FAT), not because it
is convenient to skip the host test.
