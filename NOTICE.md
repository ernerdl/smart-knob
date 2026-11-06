# Licensing notice

## Scope

| what | license | file |
|---|---|---|
| Source code — `firmware/`, `mac/`, `app/`, build configuration | MIT | [LICENSE](LICENSE) |
| Documentation and 3D models — `README.md`, `docs/`, `hardware/` | CC BY 4.0 | [LICENSE-docs.md](LICENSE-docs.md) |

## Third-party code

**`firmware/src/MagneticSensorMT6701SSI.cpp`**
**`firmware/src/MagneticSensorMT6701SSI.h`**

Unmodified copies of the MT6701 SSI driver from the SimpleFOC drivers library:
https://github.com/simplefoc/Arduino-FOC-drivers (`src/encoders/mt6701/`)

> MIT License — Copyright (c) 2021 Richard Unger

Only an attribution header was prepended; the code itself is byte-for-byte
identical to upstream.

## Dependencies

This project builds on the [SimpleFOC](https://simplefoc.com) library, also
MIT licensed. It is fetched at build time by PlatformIO and is not vendored
in this repository.

The hardware design was inspired by
[scottbez1/smartknob](https://github.com/scottbez1/smartknob); the code and
hardware here are independent work.
