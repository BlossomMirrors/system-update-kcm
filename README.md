# blossom-kcm-software-update

KDE System Settings module (KCM) for managing OS image updates on immutable Linux systems powered by [rpm-ostree](https://coreos.github.io/rpm-ostree/) such as Fedora Kinoite, BlossomOS, and Universal Blue variants.

## Features

- View the currently booted version and any pending or previous deployments
- Download and stage an OS update in the background with live progress
- Roll back to the previous deployment
- Enable or disable automatic nightly updates via the `rpm-ostreed-automatic.timer` systemd unit
- Kirigami-based UI that integrates natively into KDE System Settings

## Requirements

**Runtime**

- KDE Plasma 6 with KCMUtils
- Qt 6 (Core, DBus, Qml, Quick)
- KFrameworks 6: CoreAddons, KI18n, KCMUtils
- `rpm-ostree` daemon running on the system bus

**Build**

- CMake ≥ 3.20
- Ninja
- Extra CMake Modules (ECM)
- Qt 6 development headers (Core, DBus, Qml, Quick)
- KF6 development headers (CoreAddons, KI18n, KCMUtils)

## Building

```bash
cmake -B build -S . -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
ninja -C build
```

## Installing

```bash
sudo ninja -C build install
```

The plugin is installed to `$libdir/qt6/plugins/plasma/kcms/systemsettings/kcm_software_update.so`. KDE System Settings will pick it up automatically under **System &rarr; Software Update**.

## Packaging (RPM)

```bash
./build.sh
```

Produces an RPM under `rpmbuild/RPMS/`.

## License

[MIT](LICENSE)
