#include <switch.h>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "debug.hpp"
#include "sslmitm_service.hpp"

namespace ams::mitm::ssl {

    /* ─── WHITELIST de JOGOS, configurável POR ARQUIVO (sem rebuild) ─────────────────────────────
     * Arquivo: sdmc:/atmosphere/contents/4200000000000053/mitm_include.txt
     * Uma linha por program-id (16 hex) de JOGO a INTERCEPTAR (habilitar online). Comentário '#'.
     * O poll thread do main recarrega a cada ~2s (ReloadWhitelist) → editar reflete sem reboot.
     *
     * Política (DAEMON-ONLY por padrão; jogos = opt-in puro por arquivo, SEM default hardcoded):
     *  - DAEMONS de sistema (program_id < 0x0100000000010000, exceto am + library-applets): interceptados
     *    AUTOMATICAMENTE. São invisíveis, precisam do bypass device-PKI (b1) com prodinfo em branco
     *    (npns/nim/olsc/account/friends/bcat) e não crasham o mitm.
     *  - APLICAÇÕES (jogos, >= 0x10000): por padrão NUNCA interceptados. Interceptar a conexão NEX/Pia
     *    PRÓPRIA do jogo aplica o flip device-PKI (b1) nela e MANGA o handshake de matchmaking/P2P →
     *    2618-0521 (confirmado em servidor Nextendo, ago/2026: nosso sysmodule só passa lá com o whitelist
     *    de jogos VAZIO). Contra servidores que já servem cert confiável (Nextendo, ou o nosso via exefs
     *    disable_ca_verification), o online do jogo funciona NATIVO — não precisa tocar no jogo.
     *  - SEM default hardcoded de jogos: o sysmodule é drop-in (não DEPENDE do mitm_include). Ausente =
     *    whitelist vazia = só-daemons. O arquivo abaixo é só um ESCAPE-HATCH opcional: se um servidor
     *    específico exigir interceptar um jogo, liste o program-id nele (opt-in). */
    namespace {
        constexpr size_t MaxWhitelist = 64;
        u64  g_whitelist[MaxWhitelist];
        size_t g_whitelist_count = 0;
        os::SdkMutex g_whitelist_mutex;

        /* OPCIONAL. Ausente/vazio = daemon-only (config segura p/ Prelude/Nextendo). Sem defaults. */
        constexpr const char *IncludePath = "sdmc:/atmosphere/contents/4200000000000053/mitm_include.txt";
    }

    void ReloadWhitelist() {
        // ⚠️ NUNCA usar stdio (fopen/fgets/fclose) aqui: no sysmodule Horizon o `fopen` do newlib
        // cai em `__sfp`/`_malloc_r` (heap reentrante sem arena/lock própria) e FAULTA a thread de
        // poll -> fatal AFE2 (prog 4200…0053), boot-loop. Confirmado por backtrace de crash real
        // (0x2204 ReloadWhitelist -> _fopen_r -> __sfp -> _malloc_r). Lemos via ams::fs (a MESMA API
        // que o main já usa pro verbose_ssl.flag; usa o allocator do módulo, thread-safe).
        u64 tmp[MaxWhitelist]; size_t n = 0;
        static char rb[2048];   // só o poll thread chama -> buffer estático (não estoura a stack de 8KB)
        ams::fs::FileHandle file;
        if (R_SUCCEEDED(ams::fs::OpenFile(std::addressof(file), IncludePath, ams::fs::OpenMode_Read))) {
            s64 sz = 0;
            if (R_SUCCEEDED(ams::fs::GetFileSize(std::addressof(sz), file)) && sz > 0) {
                size_t rd = static_cast<size_t>(sz);
                if (rd > sizeof(rb) - 1) rd = sizeof(rb) - 1;
                if (R_SUCCEEDED(ams::fs::ReadFile(file, 0, rb, rd))) {
                    rb[rd] = '\0';
                    const char *p = rb;
                    while (*p != '\0' && n < MaxWhitelist) {
                        while (*p == ' ' || *p == '\t') p++;
                        if (*p != '#' && *p != '\n' && *p != '\r' && *p != '\0') {
                            u64 v = strtoull(p, nullptr, 16);   // strtoull é puro (não aloca) -> seguro
                            if (v != 0) tmp[n++] = v;
                        }
                        while (*p != '\0' && *p != '\n') p++;   // pula pro fim da linha
                        if (*p == '\n') p++;
                    }
                }
            }
            ams::fs::CloseFile(file);
        }
        std::scoped_lock lk(g_whitelist_mutex);
        g_whitelist_count = n;
        for (size_t i = 0; i < n; i++) g_whitelist[i] = tmp[i];
    }

    /* Jogos NÃO são interceptados por padrão (whitelist vazia = sem defaults). Só se o program-id
     * estiver no mitm_include.txt OPCIONAL — escape-hatch por-servidor, nunca requerido. */
    static bool IsGameWhitelisted(u64 pid) {
        std::scoped_lock lk(g_whitelist_mutex);
        for (size_t i = 0; i < g_whitelist_count; i++) if (g_whitelist[i] == pid) return true;
        return false;
    }

    SslMitmService::SslMitmService(std::shared_ptr<::Service> &&s, const sm::MitmProcessInfo &c)
        : sf::MitmServiceImplBase(std::forward<std::shared_ptr<::Service>>(s), c) {
        LogFormat("ssl connect: tid=%016" PRIx64 " pid=%" PRIu64, c.program_id.value, c.process_id);
    }

    bool SslMitmService::ShouldMitm(const sm::MitmProcessInfo &client_info) {
        const u64 pid = client_info.program_id.value;
        if (pid == 0x00FF0000A53BB900ull) return false;                                    // sysmodule NPShop
        if (pid == 0x0100000000000023ull) return false;                                    // am (User Break se manglar)
        if (pid >= 0x0100000000001000ull && pid <= 0x0100000000001FFFull) return false;    // library applets (web-applet -> FATAL)
        if (pid <  0x0100000000010000ull) return true;                                     // DAEMONS de sistema -> auto (npns/nim/olsc/account/...)
        return IsGameWhitelisted(pid);                                                     // JOGOS: só whitelist (default + mitm_include.txt)
    }

}
