# `ssl_mitm` behavior map — everything the sysmodule does

Program-id **`4200000000000053`**. It MITMs the `ssl` and `ssl:s` services and
does exactly two functional things (plus opt-in diagnostics). Everything is
scoped, fatal-proofed, and reversible. This document is the spec: enough to
audit the component **or** reimplement it yourself under your own license.

---

## 0. One-paragraph model

The `ssl` sysmodule serves every TLS user on the console (games, the eShop
webview, and the system daemons that do device/account auth). NPShop's MITM sits
transparently in front of it: it forwards every IPC command untouched, then
**rewrites two specific failure results to success** on the response path. That
is the whole mechanism — it never rewrites request payloads, never reads app
buffers in the steady state, and needs no per-firmware offsets.

---

## 1. What it intercepts — `ShouldMitm` (daemon-only)

`source/sslmitm_service.cpp`:

```cpp
bool SslMitmService::ShouldMitm(const sm::MitmProcessInfo &client_info) {
    const u64 pid = client_info.program_id.value;
    if (pid == 0x00FF0000A53BB900ull) return false;                                 // NPShop's own sysmodule
    if (pid == 0x0100000000000023ull) return false;                                 // am  (User Break if mangled)
    if (pid >= 0x0100000000001000ull && pid <= 0x0100000000001FFFull) return false; // library applets (web-applet FATALs)
    if (pid <  0x0100000000010000ull) return true;                                  // system DAEMONS -> auto
    return IsGameWhitelisted(pid);                                                  // GAMES: opt-in only (empty by default)
}
```

- **System daemons** (`program_id < 0x0100000000010000`) are intercepted
  automatically: `account`, `nim`, `olsc`, `npns`, `friends`, `bcat`… These are
  the processes that do device/account auth and **need the device-PKI bypass**
  under a blank PRODINFO.
- **Games** (`>= 0x…00010000`) are **NOT intercepted by default.** The whitelist
  (`mitm_include.txt`) is an optional escape-hatch and is empty unless you add
  ids. There is **no hardcoded default** anymore.

### ⚠️ The single most important operational fact

**Do not intercept games.** Applying the overrides below to a game's *own*
NEX/Pia TLS mangles the matchmaking/P2P handshake and surfaces as
**`2618-0521`**. Against servers that already present a trusted cert (Nextendo,
or ours via `disable_ca_verification`) the game's online path works **natively**
— leave it alone. This is why the default is daemon-only. (We learned this the
hard way: an earlier build hardcoded MK8D/Splatoon 2/Smash into the whitelist
and it broke Nextendo online until the list was emptied.)

Excluded on purpose: `am` and library-applets FATAL if their `ssl` traffic is
mangled, so they're never intercepted.

---

## 2. The two overrides — where the work happens

The forwards and the result-rewrites live in a **patched `libstratosphere`**
(`ServerSession::ForwardRequest`, in
`libstratosphere/source/sf/hipc/sf_hipc_server_session_manager.cpp`). The
sysmodule turns them on at boot:

```cpp
// source/sslmitm_main.cpp — Main()
sf::hipc::SetForwardRequestLogging(true);       // enables the parse of cmd/result on forwards (this process only)
sf::hipc::SetOverrideRegisterInternalPki(true); // enables b1 + b2 below
```

`ForwardRequest` forwards the command, then scans the **response** for the
`CmifOutHeader` (magic `SFCO`) and reads `.result`:

### b1 — device-PKI (`RegisterInternalPki`) → success

```cpp
// RegisterInternalPki / ImportClientPki returns 0x167b (2123-0011) because a
// blank PRODINFO has no real device client-PKI. We force SUCCESS so the daemon
// can build a server-auth-only TLS context (no client cert), which the server
// accepts. This is the ONLY thing standing between a blank PRODINFO and online.
if (g_override_reg_internal_pki && out_hdr != nullptr && log_result == 0x167b) {
    out_hdr->result = 0x0;
}
```

This is the crux for blank PRODINFO. It fires on **any** intercepted session
whose result is exactly `0x167b` — i.e. only when the device client-PKI step
actually failed, which is exactly the blank-PRODINFO case. A real PRODINFO never
produces `0x167b` here, so the override is a no-op for real-PRODINFO users.

### b2 — server-cert verification on specific contexts → success

```cpp
// DoHandshake (cmd 9 = user context, cmd 8 = game-server / obj context) returns
// a cert-verify error when the *server's* leaf isn't trusted. On domains, for
// cmd 8/9, anything that isn't 0x0 (done) / 0x1987b (WouldBlock) / 0x167b (b1)
// is a verification failure -> force success.
else if (g_override_reg_internal_pki && out_hdr != nullptr && (log_cmd_id == 9 || log_cmd_id == 8)
         && log_result != 0x0 && log_result != 0x1987b && log_result != 0x167b && log_dom_type == 1) {
    out_hdr->result = 0x0;
}
```

b2 overlaps in intent with your `disable_ca_verification` exefs patch (server
cert trust). The difference: b2 is a **runtime** flip scoped to the handshake
commands, so it's firmware-independent and also catches the game-server /
webview contexts a static patch may miss. **Because b2 fires on intercepted
sessions, and games are not intercepted, it never touches a game's handshake** —
consistent with §1.

> Note: an earlier `b3` (SetVerifyOption override) was **removed** — the `ssl:s`
> context rejects a changed VerifyOption (`2123-0133`); trust must come from a
> real/accepted server cert, not from lowering the client's verify option.

---

## 3. What it deliberately does NOT do

- **No request-payload rewriting.** Requests are forwarded byte-for-byte.
- **No app-buffer reads in steady state.** An earlier diagnostic that
  dereferenced send/recv buffers of arbitrary processes could fault on a page
  boundary and FATAL the module; it was removed. Normal operation reads only the
  module's own response buffer (always mapped). The only optional app-buffer
  read is a bounded SNI log, gated behind the verbose flag.
- **No PRODINFO use.** It doesn't read or need the device cert — that's the
  whole point.
- **No phone-home.** It only rewrites two local IPC results.
- **No per-firmware offsets.** It acts on IPC result codes, so it is
  **firmware-independent** (works across HOS versions as long as the `ssl` IPC
  shape holds). This is the main advantage over a static binary patch.

---

## 4. Why b1 can't be a static IPS (the RE, for reviewers)

We wanted a static `exefs_patch` for b1 (it would fit Prelude's model perfectly).
It isn't cleanly possible on 22.5.0. Disassembling the `ssl` main NSO
(build-id `558136EE…`):

- **`0x167b` is never materialized by a `movz`/`mov` immediate — 0 occurrences.**
- It comes from a **result table in `.rodata`** (`0x26f100…`), one 32-bit
  `nn::Result` per internal-error index. Index 0 (`0x0000167b`) is the catch-all;
  many different internal cert errors funnel into it (the table is mostly
  `0x00001a7b` = 2123-0013, with `0x167b` scattered at ~20 indices):

  ```
  0x26f100: 0000167b 00019a7b 00001a7b 00001a7b   <- index 0 = 0x167b (catch-all)
  0x26f1a0: 00001a7b 0000167b 0000167b 00001a7b
  0x26f300: 000bd27b 0000167b 0000167b 0000167b
  ...
  ```
- **No `adrp` references the table's page (`0x26f000`) — 0 xrefs.** The base is
  computed indirectly (base+index), so there's no instruction to hook and no
  single site to NOP.

Zeroing the table entry would be **global and destructive** (it would swallow
every error that maps to `0x167b`, and wouldn't isolate device-PKI). So the
clean way to force **only** the device-PKI result is at the **IPC boundary, on
the specific command** — i.e. b1 in the MITM. That's the justification for the
sysmodule existing at all.

(Server-cert verification, by contrast, *is* a specific branch and *is*
statically patchable — that's exactly what `disable_ca_verification` does, and
what our paired `ssl_ca` IPS for `558136EE…` does: 3 branch writes at
`0x106980 / 0x1079cc / 0x107a20`.)

---

## 5. Robustness / operational notes (things that will bite a naive port)

- **Never use stdio in the sysmodule.** `fopen`/`malloc` from newlib fault in a
  Horizon sysmodule (reentrant heap, no arena). All file reads use `ams::fs`
  (`OpenFile`/`GetFileSize`/`ReadFile`) with static buffers. A prior `fopen` in
  the whitelist reload thread caused a boot-loop FATAL.
- **Domain object pool is enlarged.** Heavy online games open thousands of
  domain SSL objects; the shared pool aborts if exceeded. Tunings in
  `SslMitmManagerOptions`: `MaxDomainObjects=0x2000`, `MaxDomains=0x40`,
  `MaxSessions=48`. (Grows `.bss` only.) With daemon-only interception the
  pressure is far lower, but the headroom is kept.
- **Fatal-proof boot.** SD mount failures are retried best-effort and never
  abort; the module runs without the SD (SD is only for optional logs).
- **Both ports.** Registers `ssl:s` (system — the download/daemon path) and
  `ssl` (user). Aborts only if **both** fail to register.
- **Runtime verbose toggle.** Creating
  `sdmc:/atmosphere/contents/4200000000000053/flags/verbose_ssl.flag` turns on
  per-op logging + reloads the optional whitelist within ~2s (no reboot). Off by
  default; b1/b2 run regardless.

---

## 6. Dependencies / pairing

- **Server-cert trust:** keep Prelude's existing `disable_ca_verification`
  (exefs) — it covers the general server-cert path across processes. b2 is a
  runtime complement for the handshake contexts, not a replacement.
- **Blank PRODINFO:** the whole value proposition. Set
  `blank_prodinfo_emummc=1` for the anonymous mode (see integration guide).
- **No `mitm_include.txt` required** — absent = daemon-only = correct default.
