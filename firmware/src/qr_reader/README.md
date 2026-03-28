# Vendored QR Sources

This directory contains vendored source code copied from upstream projects for
the Zauberbox QR scanning pipeline. These files are not original project code
and must retain clear attribution when updated.

## Upstream Sources

### 1. ESP32QRCodeReader

Source repository:

- https://github.com/alvarowolfx/ESP32QRCodeReader

Vendored files:

- `ESP32QRCodeReader.cpp`
- `ESP32QRCodeReader.h`

Local upstream reference files:

- `README.upstream.md`
- `LICENSE.upstream`

### 2. quirc

Source repository:

- https://github.com/dlbeer/quirc

Vendored files:

- `quirc/quirc.h`
- `quirc/quirc.c`
- `quirc/quirc_internal.h`
- `quirc/decode.c`
- `quirc/identify.c`
- `quirc/version_db.c`

Local upstream reference files:

- `quirc/README.upstream.md`
- `quirc/LICENSE.upstream`

### 3. OpenMV-derived utility code

Source repository:

- https://github.com/openmv/openmv

Vendored files:

- `openmv/collections.c`
- `openmv/collections.h`
- `openmv/fmath.h`

Local upstream reference files:

- `openmv/README.upstream.md`
- `openmv/LICENSING.upstream.md`

## Maintenance Note

When updating any vendored file here:

- preserve upstream copyright and license notices
- keep the matching upstream README/license files in sync
- document any local modifications in commit messages or adjacent comments when
  they are needed for this firmware build
