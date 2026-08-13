# Integration guide — dropping `ssl_mitm` into Prelude

This maps the component onto Prelude's existing delivery flow (`copyTreeRomfs
"romfs:/sd" -> "sdmc:"` + `nextendo_apply.c`). No new build machinery is
required in Prelude if you take the prebuilt route.

## 0. Prerequisites (you already have these)

- `disable_ca_verification` exefs patch (server-cert trust). Keep it.
- `iniSetBlankProdinfoEmummc(bool)` in `nextendo_apply.c` (you already toggle
  this for Nintendo mode). We reuse it.

## 1. Ship the sysmodule as SD content

The sysmodule is standard Atmosphère boot2 content. Place its build output where
your existing tree-copy already delivers files:

```
romfs/sd/atmosphere/contents/4200000000000053/exefs.nsp
romfs/sd/atmosphere/contents/4200000000000053/toolbox.json
```

`toolbox.json` (lets users see/disable it in Daybreak/haku33 etc.):

```json
{ "name": "npshop-ssl-mitm", "tid": "4200000000000053", "requires_reboot": true }
```

`copyTreeRomfs("romfs:/sd", "sdmc:")` already copies this. Atmosphère loads any
`contents/<tid>/exefs.nsp` at boot2 automatically — **no NRO code is needed to
"start" it.** A reboot (which Prelude already does after applying) activates it.

> License placement: keep the component's `LICENSE` (GPLv2) next to it, e.g.
> `romfs/sd/atmosphere/contents/4200000000000053/LICENSE`, and a `SOURCE.url`
> pointing at the source repo. It stays an independent aggregated unit — see
> README option (a)/(b)/(c) if you'd rather not carry GPLv2 in-tree.

## 2. Wire the "anonymous online" toggle

The only NRO-side change is choosing the PRODINFO mode. Today Nextendo mode uses
real PRODINFO (`blank_prodinfo_emummc=0`). Add an **anonymous** variant that
sets it to `1`:

```c
// In the Nextendo-mode apply path, when the user opts into anonymous online:
iniSetBlankProdinfoEmummc(true);   // no real serial / device cert on the wire
// (real-PRODINFO Nextendo mode stays the default; nothing changes for it)
```

Recommended UX: a sub-option under Nextendo mode — "Anonymous (blank PRODINFO)"
vs the current "Standard (real PRODINFO)". Standard stays default.

Nothing else is required: the sysmodule is daemon-only out of the box, so games
are never intercepted and no `mitm_include.txt` is shipped.

## 3. Enable / disable (clean, reversible)

- **Enable:** presence of `contents/4200000000000053/` + reboot. (Your apply
  step already copies + reboots.)
- **Disable:** remove `contents/4200000000000053/` (or drop a
  `contents/4200000000000053/flags/boot2.flag` gate if you prefer a soft
  switch), and set `blank_prodinfo_emummc` back to `0`. Your existing cleanup
  that removes Prelude's changes can delete the directory the same way it
  removes patches.

## 4. Firmware coverage

- The **sysmodule** is firmware-independent (acts on IPC result codes, not
  offsets) — one binary across HOS versions.
- The paired **`disable_ca_verification`** is per-build-id, exactly like the
  ~29 build-ids you already ship. No new per-firmware work beyond what you do.

## 5. What to validate on-device (we have not yet run *this exact packaging*)

Our field test was NPShop's full kit; the daemon-only change and the
Prelude-packaging are new. Confirm on a 22.5.0 console, emuMMC, Nextendo mode:

1. **Anonymous mode:** `blank_prodinfo_emummc=1` + component installed →
   online auth completes, **no `2123-0011`**, games connect (native, not
   intercepted), no `2618-0521`.
2. **Standard mode regression:** `blank_prodinfo_emummc=0` + component installed
   → identical to today (b1 is a no-op with real PRODINFO).
3. **Games:** confirmed not intercepted (no `mitm_include.txt` present).

## 6. Support surface you're taking on (be aware)

If you carry the component, the operational notes in the behavior map §5 (no
stdio in-sysmodule, domain-pool sizing, fatal-proof boot) are the sharp edges.
They're already handled in the code; they matter if you ever fork/modify it.
