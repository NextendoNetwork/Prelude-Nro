# Prelude

**Prelude** is the Nintendo Switch homebrew that switches your console between the
[Nextendo Network](https://nextendo.network) and Nintendo's official servers.

It is a small `.nro` you run from the homebrew menu. You pick a mode, it applies the change and
reboots. Nothing is permanent — you can switch back whenever you want.

```
        Which network do you want to load?
        [ NEXTENDO ]     [ NINTENDO ]
```

- **Nextendo mode** — redirects Nintendo traffic to the Nextendo servers and provisions everything
  the console needs to trust them.
- **Nintendo mode** — removes every component Prelude installed and hands the console back to
  Nintendo, with Atmosphère's own telemetry blocking restored.

## How it works

Prelude does not patch games and does not touch the network stack. It writes configuration that
Atmosphère already understands, then reboots so the changes take effect:

| What | Where |
| --- | --- |
| Host redirections | `/atmosphere/hosts/{sysmmc,emummc}.txt` (Atmosphère DNS.mitm) |
| DNS.mitm on/off | `/atmosphere/config/system_settings.ini` |
| PRODINFO per mode | `/exosphere.ini` (emuMMC only) |
| Certificate trust | `exefs_patches/`, `nro_patches/`, browser CA bundle |

Because it only writes files Atmosphère reads at boot, everything it does is reversible by
switching modes — or by deleting those files by hand.

## Building

There is no local toolchain requirement beyond Docker:

```sh
docker run --rm -v "$PWD:/work" -w /work devkitpro/devkita64 \
  bash -c 'dkp-pacman -Syu --noconfirm switch-freetype switch-sdl2 switch-sdl2_mixer switch-mpg123 && make -j$(nproc)'
```

The result is `nextendo.nro`. Copy it to `/switch/` on your SD card.

Version numbers are kept in lockstep: `APP_VERSION` in the `Makefile` is `1.0.N`, where `N` is
`NEXTENDO_BUILD` in `source/nextendo_update.h`, and the GitHub tag is `vN`.

## Status

**Prelude is not actively maintained right now.** It is published here so anyone can read it, build
it, fork it, or run their own network with it. Issues and pull requests may go unanswered.

If you are running your own server, the addresses Prelude redirects to live in
`source/nextendo_hosts.h` — that file is the whole configuration surface.

## Licence

Copyright (C) 2026 Nextendo Network.

Prelude is free software, licensed under the **GNU Affero General Public License, version 3 or
later**. You may use, study,
share and modify it. If you distribute a modified version, or run one as a network service, you
must release your changes under the same licence.

See [LICENSE](LICENSE) for the full text, or <https://www.gnu.org/licenses/agpl-3.0.html>.

This project is not affiliated with, endorsed by, or connected to Nintendo. "Nintendo" and
"Nintendo Switch" are trademarks of Nintendo. Use it on hardware you own.
