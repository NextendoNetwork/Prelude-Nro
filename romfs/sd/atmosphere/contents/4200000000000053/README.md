# `4200000000000053` — NPShop `ssl` MITM sysmodule

Self-contained Atmosphère boot2 sysmodule. Copied to the SD by Prelude's
existing `copyTreeRomfs("romfs:/sd", "sdmc:")`; Atmosphère loads it at boot.

- **What it does:** rewrites two `ssl` IPC failure results to success so a
  **blank PRODINFO** console can go online (device-PKI `2123-0011` bypass on the
  system daemons). Daemon-only — never intercepts games. Full details:
  `contrib/npshop-ssl-mitm/MITM_BEHAVIOR_MAP.md`.
- **No-op unless anonymous:** with a real PRODINFO the bypass never triggers, so
  installing it changes nothing until `blank_prodinfo_emummc=1` is set.
- **No Atmosphère modification:** `libstratosphere` is statically linked into
  `exefs.nsp`; the CFW is untouched.
- **License:** GPLv2 (see `LICENSE` in this directory). Independent process,
  aggregated on the SD — not linked into the Prelude NRO.

## Files

- `exefs.nsp` — the sysmodule binary. **Prebuilt drop-in.**
- `toolbox.json` — metadata (name / tid / requires_reboot).
- `LICENSE` — GPLv2.

Source + rebuild instructions: `contrib/npshop-ssl-mitm/`.
