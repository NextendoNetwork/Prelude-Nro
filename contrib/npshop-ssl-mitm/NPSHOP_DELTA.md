# The actual NPShop delta to `libstratosphere` (isolated)

⚠️ **Read this instead of trusting the raw `libstratosphere.patch`.** That patch
diffs our file against a very old (2018–2019) Atmosphère import in our fork's
git history, so it also contains **years of upstream Atmosphère API
modernization** (`armGetTls`→`GetMessageBufferOnTls`, `pointer_buffer`→
`m_pointer_buffer`, `ResultSuccess()`→`R_SUCCEED()`, `std::construct_at`, etc.).
**None of that is ours** — it's stock Atmosphère. Our real functional change is
small and lives entirely inside `ServerSession::ForwardRequest`, plus a few file-
scope toggles. Everything below is marked with `// NPShop` in the source.

Only **one file** is touched:
`libstratosphere/source/sf/hipc/sf_hipc_server_session_manager.cpp`.

## 1. File-scope toggles (added, off by default)

```cpp
namespace ams::log { void LogFormatImpl(const char *fmt, ...); }   // fwd decl, resolved in the module

namespace ams::sf::hipc {
    constinit bool g_forward_request_log_enabled = false;
    void SetForwardRequestLogging(bool e)      { g_forward_request_log_enabled = e; }
    constinit bool g_verbose_log = false;
    void SetForwardRequestVerboseLog(bool e)   { g_verbose_log = e; }
    constinit bool g_override_reg_internal_pki = false;
    void SetOverrideRegisterInternalPki(bool e){ g_override_reg_internal_pki = e; }
    // (g_rewrite_nsd_fqdn / SetRewriteNsdFqdn also exist here but belong to a SEPARATE
    //  nsd_mitm module, not ssl — ignore for this contribution.)
}
```

All default to `false`. Only NPShop's `ssl_mitm` process flips
`g_forward_request_log_enabled` + `g_override_reg_internal_pki` on. Other
consumers that share this `libstratosphere` (`nsd`, `bsd`, `ldn`…) leave them
off, so their behavior is byte-identical to stock.

## 2. Inside `ForwardRequest` — the only behavioral change

Stock `ForwardRequest` copies the saved TLS message, forwards it, and closes
copy-handles. NPShop adds, **all gated on the toggles above**:

- **(a)** a read-only parse of the *request* to recover `cmd_id` / `object_id` /
  `domain_type` — reads only the module's own command buffer.
- **(b)** an optional bounded SNI log (verbose-only).
- **(c)** a parse of the *response* `CmifOutHeader.result`, then the two
  overrides:

```cpp
// b1 — device-PKI: RegisterInternalPki returns 0x167b (2123-0011) with a blank
// PRODINFO. Force success so a server-auth-only TLS context can be built.
if (g_override_reg_internal_pki && out_hdr && log_result == 0x167b) {
    out_hdr->result = 0x0;
}
// b2 — server-cert verify on DoHandshake (cmd 8/9, domain): any non-0x0 /
// non-WouldBlock / non-0x167b result is a verification failure -> success.
else if (g_override_reg_internal_pki && out_hdr && (log_cmd_id == 9 || log_cmd_id == 8)
         && log_result != 0x0 && log_result != 0x1987b && log_result != 0x167b && log_dom_type == 1) {
    out_hdr->result = 0x0;
}
```

That's the entire functional contribution: **two conditional result rewrites**
on the MITM forward path, behind a runtime toggle. No request payloads are
altered; no app buffers are read in the non-verbose path.

## How to get a clean diff (if you want one)

The honest base is the exact upstream Atmosphère commit our `libstratosphere` is
pinned to — not our fork's ancient import. To produce a minimal diff:

```bash
# fetch the stock file at the AMS version this libstrat tracks, then:
diff -u stock_sf_hipc_server_session_manager.cpp \
        bundle/libstratosphere-patch/source/sf/hipc/sf_hipc_server_session_manager.cpp
```

The only hunks that should appear are §1 and §2 above. Everything else in the
raw `libstratosphere.patch` is upstream Atmosphère, not part of this proposal.
