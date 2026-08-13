# Prelude ⇄ NPShop `ssl_mitm` — contribution package

**What this is.** A self-contained proposal + code drop so the Prelude team can
decide whether (and how) to offer an **anonymous / blank-PRODINFO online mode**
on top of Prelude, reusing NPShop's `ssl` MITM sysmodule. Nothing here is
merged or assumed — it's a documented component and an integration path for you
to evaluate.

**Why you might want it.** Prelude's Nextendo mode currently needs a **real
PRODINFO** (`blank_prodinfo_emummc=0`), so the console still presents its real
device certificate / serial to the servers. This component lets online work
with **`blank_prodinfo_emummc=1`** — no real device identity on the wire — while
still connecting to Nextendo's servers. In our testing the Nextendo servers
accept a blank-PRODINFO console **as-is**; no server change is required.

> **Field-tested (Aug 2026):** NPShop's sysmodule running on a *fully-Nextendo*
> setup with a blank PRODINFO reaches online end-to-end. The one gotcha we hit
> and fixed is documented below (don't intercept games — see the behavior map).

## The three documents

| File | What it covers |
|---|---|
| [`MITM_BEHAVIOR_MAP.md`](./MITM_BEHAVIOR_MAP.md) | Exactly what the sysmodule does, every override, the policy, and *why* each exists. Written so you can **either** ship our component **or** reimplement it yourself. |
| [`INTEGRATION_GUIDE.md`](./INTEGRATION_GUIDE.md) | Concrete steps to drop it into Prelude's existing `copyTreeRomfs` + `nextendo_apply.c` flow, plus the `blank_prodinfo_emummc` toggle. |
| [`code/MANIFEST.md`](./code/MANIFEST.md) | The source that makes up the component + a script to bundle it byte-exact. |

## The honest caveats (so you can decide with eyes open)

1. **It's a sysmodule.** Prelude today ships **zero** sysmodules — it writes
   config + copies static patch files. This adds a background process
   (program-id `4200000000000053`). That's a philosophy change for your project;
   we're not assuming you want it.

2. **License.** The sysmodule derives from `ldn_mitm` + Atmosphère
   `libstratosphere`, both **GPLv2-only** (no "or later"). Prelude is
   **AGPL-3.0+**. GPLv2-only and AGPLv3 **cannot be combined into one linked
   work** — but this component is a **separate Horizon process** that talks to
   your NRO only at arm's length (it doesn't link against Prelude), so shipping
   it alongside Prelude on the SD is **mere aggregation**, which both licenses
   allow. Practical options:
   - **(a)** Carry the prebuilt component in-tree as an independent GPLv2 unit
     (its own LICENSE + source pointer), copied to the SD by your installer.
   - **(b)** We host it; Prelude just references/fetches it. Nothing GPLv2 in
     your repo.
   - **(c)** You **reimplement** an equivalent MITM under your own license using
     the behavior map as the spec (it's small — the whole mechanism is ~2
     overrides on the IPC result). Then there's no third-party license at all.

3. **Why a sysmodule and not a static IPS (which would fit your model better).**
   We checked. The device-PKI failure that blocks blank PRODINFO
   (`2123-0011` / `0x167b`) is **never a direct immediate** in the `ssl` binary
   — it comes from a shared `.rodata` result table (index 0 = the catch-all),
   reached by base+index with **no `adrp` xref to hook**. So there is no clean
   static patch for it; forcing it requires acting on the **IPC result at
   runtime**, which is what the sysmodule does. (Server-cert verification, by
   contrast, *is* statically patchable — that's your existing
   `disable_ca_verification`.) Details + the RE dump are in the behavior map.

**The ask:** read the map, decide if any of (a)/(b)/(c) is interesting. If yes,
the integration guide is the drop-in path. If not, no harm — the map still
documents a mechanism you're free to use.
