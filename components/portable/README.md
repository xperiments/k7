# Portable components

Everything here compiles on the host build as well as the firmware. `host/Makefile`
lists every `.c` in this directory, and `host/tests/layering.sh` H3 fails if one is
missing. These components may not include ESP-IDF-only headers or branch on the target.

Vendored sources (`shine`, `dr_libs`) keep their own licences and are never modified.
