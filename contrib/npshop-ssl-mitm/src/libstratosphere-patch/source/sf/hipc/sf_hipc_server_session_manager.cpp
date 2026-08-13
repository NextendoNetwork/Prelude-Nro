/*
 * Copyright (c) Atmosphère-NX
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stratosphere.hpp>

/* NPShop ssl-mitm diag: logger definido no módulo (debug.cpp) — resolvido no link. */
namespace ams::log { void LogFormatImpl(const char *fmt, ...); }

namespace ams::sf::hipc {

    /* NPShop ssl-mitm: gate p/ logar cmd/obj/result de TODO comando forwardado.
     * Fica OFF por padrão; só o ssl_mitm liga (via SetForwardRequestLogging(true)),
     * então nsd/bsd/ldn que compartilham esta libstratosphere NÃO logam aqui. */
    constinit bool g_forward_request_log_enabled = false;
    void SetForwardRequestLogging(bool enabled) { g_forward_request_log_enabled = enabled; }

    /* NPShop PERF: log VERBOSE por-operação (SSL WRITE/RESP/obj-cmd-result) grava no SD a CADA
     * IPC do ssl — isso deixa TODA a rede do console lenta (todo app que usa TLS passa aqui).
     * OFF por padrão. Os fixes b1/b2 (parse+override do result) rodam INDEPENDENTE disto, sem
     * I/O. Ligar só p/ debug (via SetForwardRequestVerboseLog). */
    constinit bool g_verbose_log = false;
    void SetForwardRequestVerboseLog(bool enabled) { g_verbose_log = enabled; }

    /* NPShop ssl-mitm: override RegisterInternalPki (ISslContext cmd 8) que falha 0x167b
     * (2123-0011) -> 0x0. Redirecionamos o download p/ nosso server (sem mTLS/client-cert),
     * então o nim não precisa do PKI de device real; só não pode falhar nesse passo. */
    constinit bool g_override_reg_internal_pki = false;
    void SetOverrideRegisterInternalPki(bool enabled) { g_override_reg_internal_pki = enabled; }

    /* NPShop nsd-mitm: reescreve a FQDN resolvida (nsd Resolve/ResolveEx, cmd 20/21)
     * ".ndas.srv.nintendo.net" -> ".ndas.srv.a.npshop.org" (MESMO tamanho: 21 chars) no
     * recv_static (pointer buffer), p/ o device-auth (dauth/aauth/dcert/acert) conectar
     * no NOSSO host com cert LE real em vez do self-signed *.nintendo.net (muro 2155-8023
     * = nnDauth curl WRITE_ERROR). Só o nsd_mitm liga (processo separado do ssl). */
    constinit bool g_rewrite_nsd_fqdn = false;
    void SetRewriteNsdFqdn(bool enabled) { g_rewrite_nsd_fqdn = enabled; }

    namespace {

        #if AMS_SF_MITM_SUPPORTED
        constexpr inline void PreProcessCommandBufferForMitm(const cmif::ServiceDispatchContext &ctx, const cmif::PointerAndSize &pointer_buffer, uintptr_t cmd_buffer) {
            /* TODO: Less gross method of editing command buffer? */
            if (ctx.request.meta.send_pid) {
                constexpr u64 MitmProcessIdTag = 0xFFFE000000000000ul;
                constexpr u64 OldProcessIdMask = 0x0000FFFFFFFFFFFFul;
                u64 *process_id = reinterpret_cast<u64 *>(cmd_buffer + sizeof(HipcHeader) + sizeof(HipcSpecialHeader));
                *process_id = (MitmProcessIdTag) | (*process_id & OldProcessIdMask);
            }

            if (ctx.request.meta.num_recv_statics) {
                /* TODO: Can we do this without gross bit-hackery? */
                reinterpret_cast<HipcHeader *>(cmd_buffer)->recv_static_mode = 2;
                const uintptr_t old_recv_list_entry = reinterpret_cast<uintptr_t>(ctx.request.data.recv_list);
                const size_t old_recv_list_offset = old_recv_list_entry - util::AlignDown(old_recv_list_entry, TlsMessageBufferSize);
                *reinterpret_cast<HipcRecvListEntry *>(cmd_buffer + old_recv_list_offset) = hipcMakeRecvStatic(pointer_buffer.GetPointer(), pointer_buffer.GetSize());
            }
        }
        #endif

    }

    #if AMS_SF_MITM_SUPPORTED
    Result ServerSession::ForwardRequest(const cmif::ServiceDispatchContext &ctx) const {
        AMS_ABORT_UNLESS(ServerManagerBase::CanAnyManageMitmServers());
        AMS_ABORT_UNLESS(this->IsMitmSession());

        /* TODO: Support non-TLS messages? */
        AMS_ABORT_UNLESS(m_saved_message.GetPointer() != nullptr);
        AMS_ABORT_UNLESS(m_saved_message.GetSize() == TlsMessageBufferSize);

        /* Get TLS message buffer. */
        u32 * const message_buffer = static_cast<u32 *>(hipc::GetMessageBufferOnTls());

        /* Copy saved TLS in. */
        std::memcpy(message_buffer, m_saved_message.GetPointer(), m_saved_message.GetSize());

        /* Prepare buffer. */
        PreProcessCommandBufferForMitm(ctx, m_pointer_buffer, reinterpret_cast<uintptr_t>(message_buffer));

        /* NPShop ssl-mitm diag: captura cmd_id + (se domínio) object_id da REQUEST
         * ANTES do forward (o SendSyncRequest sobrescreve a TLS com a resposta).
         * Somente parse/leitura — NUNCA altera o buffer. */
        u32 log_cmd_id = 0xFFFFFFFFu;
        u32 log_obj_id = 0;
        u8  log_dom_type = 0;
        /* NPShop: SÓ lê o buffer de comando do PRÓPRIO mitm (message_buffer, sempre mapeado)
         * p/ extrair cmd_id/obj_id/dom_type. Nada de ponteiro de send/recv buffer de app. */
        if (g_forward_request_log_enabled) {
            const auto req = hipcParseRequest(message_buffer);
            if (req.meta.num_data_words > 0 && req.data.data_words != nullptr) {
                const uintptr_t words_begin = reinterpret_cast<uintptr_t>(req.data.data_words);
                const uintptr_t words_end   = words_begin + static_cast<size_t>(req.meta.num_data_words) * sizeof(u32);
                const uintptr_t raw         = util::AlignUp(words_begin, 0x10);
                auto fits = [&](uintptr_t p, size_t n) -> bool { return p >= words_begin && (p + n) <= words_end; };
                /* Domínio? A raw começa com CmifInHeader (magic SFCI) se NÃO for domínio;
                 * senão com CmifDomainInHeader (type/object_id) e o CmifInHeader vem depois. */
                if (fits(raw, sizeof(u32)) && *reinterpret_cast<const u32 *>(raw) == CMIF_IN_HEADER_MAGIC) {
                    if (fits(raw, sizeof(CmifInHeader))) {
                        log_cmd_id = reinterpret_cast<const CmifInHeader *>(raw)->command_id;
                    }
                } else if (fits(raw, sizeof(CmifDomainInHeader))) {
                    const auto *dh = reinterpret_cast<const CmifDomainInHeader *>(raw);
                    log_dom_type = dh->type;
                    log_obj_id   = dh->object_id;
                    const uintptr_t cmif = raw + sizeof(CmifDomainInHeader);
                    if (dh->type == CmifDomainRequestType_SendMessage && fits(cmif, sizeof(CmifInHeader))
                        && *reinterpret_cast<const u32 *>(cmif) == CMIF_IN_HEADER_MAGIC) {
                        log_cmd_id = reinterpret_cast<const CmifInHeader *>(cmif)->command_id;
                        /* NPShop: b3 (override de SetVerifyOption) REMOVIDO. O ssl:s rejeita
                         * qualquer VerifyOption != o original nas conexões do nim (2123-0133):
                         * o ISslContext do nim exige PeerCa, então baixar a verificação via
                         * mitm mata a conexão. A confiança TLS vem do CERT LE REAL nos hosts
                         * *.ns.npshop.org (verificação nativa 0x3 passa). Ver
                         * nim-2137-8023-curl-write-error / certstore-bdf-format-22.5. */
                    }
                }
            }
        }

        /* NPShop ssl-mitm: TODO logging que dereferenciava BUFFER DE APP (DIAG cmd4/PKI,
         * HOST/SNI, WRITE do payload HTTP, RESP) foi REMOVIDO. Lia ponteiros de send/recv
         * buffer de QUALQUER processo (ex.: album/sphaira concorrente ao MK8D) e podia faltar
         * numa borda de página -> FATAL do ssl_mitm (crash report AFE2, prog 4200...0053).
         * A visibilidade do secure connect vem AGORA só do result-code + overrides b1/b2
         * abaixo, que leem exclusivamente o buffer de resposta do PRÓPRIO mitm (sempre mapeado). */

        /* NPShop DIAG SEGURO (recon S2/multi-título): loga só o SNI do SetHostName, lido do
         * send_static[0] — type-X copiado pelo kernel pro pointer_buffer do PRÓPRIO mitm (sempre
         * mapeado). NUNCA lê send_buffers (map-alias do cliente = fault em borda de página = o crash
         * do album/sphaira). Gated em verbose: normal fica sem NENHUM read de buffer de app. */
        if (g_forward_request_log_enabled && g_verbose_log) {
            const auto rqh = hipcParseRequest(message_buffer);
            const u8 *hp = nullptr; size_t hsz = 0;
            if (rqh.meta.num_send_statics > 0 && rqh.data.send_statics != nullptr) {
                const auto &d = rqh.data.send_statics[0];
                hp  = reinterpret_cast<const u8 *>(static_cast<uintptr_t>(d.address_low) | (static_cast<uintptr_t>(d.address_mid) << 32) | (static_cast<uintptr_t>(d.address_high) << 36));
                hsz = d.size;
            } else if (rqh.meta.num_send_buffers > 0 && rqh.data.send_buffers != nullptr) {
                /* SetHostName do S2 vem em send_buffer (map-alias, mapeado pelo kernel ANTES do
                 * forward). Leitura BOUNDED (<=256, o kernel mapeia >= size) = segura pré-forward. */
                const auto &d = rqh.data.send_buffers[0];
                hp = reinterpret_cast<const u8 *>(static_cast<uintptr_t>(d.address_low) | (static_cast<uintptr_t>(d.address_mid) << 32) | (static_cast<uintptr_t>(d.address_high) << 36));
                const size_t bsz = static_cast<size_t>(d.size_low) | (static_cast<size_t>(d.size_high) << 32);
                hsz = bsz < 256 ? bsz : 256;
            }
            {
                if (hp != nullptr && hsz >= 4 && hsz < 256) {
                    bool looks = true, hasDot = false; size_t L = 0;
                    for (; L < hsz; L++) {
                        const u8 c = hp[L];
                        if (c == 0) break;                 /* string termina no NUL */
                        if (c == '.') hasDot = true;
                        if (c < 0x20 || c >= 0x7f || c == ' ') { looks = false; break; }
                    }
                    if (looks && hasDot && L >= 4 && L < 200) {
                        char hb[201]; for (size_t i = 0; i < L; i++) hb[i] = static_cast<char>(hp[i]); hb[L] = '\0';
                        ::ams::log::LogFormatImpl("SSL HOST obj=%u cmd=%u host=[%s]\n", log_obj_id, log_cmd_id, hb);
                    }
                }
            }
        }

        /* Dispatch forwards. */
        R_TRY(svc::SendSyncRequest(util::GetReference(m_forward_service)->session));

        /* NPShop ssl-mitm diag: parse o CmifOutHeader.result da RESPONSE (agora na TLS)
         * e loga. O logger faz backup/restore da TLS, então a resposta forwardada
         * segue intacta — transparência preservada. */
        if (g_forward_request_log_enabled) {
            u32 log_result = 0xFFFFFFFFu;
            CmifOutHeader *out_hdr = nullptr;   /* mutável, p/ override */
            const auto resp = hipcParseResponse(message_buffer);
            if (resp.num_data_words > 0 && resp.data_words != nullptr) {
                const uintptr_t words_begin = reinterpret_cast<uintptr_t>(resp.data_words);
                const uintptr_t words_end   = words_begin + static_cast<size_t>(resp.num_data_words) * sizeof(u32);
                /* ROBUSTO (b1 alargado): varre TODA a resposta procurando o CmifOutHeader
                 * (magic SFCO) em qualquer offset de 4 bytes. Cobre non-domain, domain, e
                 * respostas com out-objects/handles que deslocam o header — que é o caso do
                 * contexto ssl:s de CONTEÚDO (es/ns), cuja resposta o parser antigo não pegava,
                 * deixando o 0x167b passar. Pega o 1o header cujo magic bate. */
                for (uintptr_t p = words_begin; p + sizeof(CmifOutHeader) <= words_end; p += sizeof(u32)) {
                    if (*reinterpret_cast<const u32 *>(p) == CMIF_OUT_HEADER_MAGIC) {
                        out_hdr = reinterpret_cast<CmifOutHeader *>(p);
                        break;
                    }
                }
            }
            if (out_hdr != nullptr) { log_result = out_hdr->result; }
            /* NPShop FIX b1: RegisterInternalPki (ImportClientPki) do device retorna
             * 0x167b (2123-0011) — o client-PKI não existe p/ nossos hosts. Forçamos
             * SUCESSO (0x0) para o nim montar o contexto TLS server-auth-only, que
             * nosso servidor CA-injetado aceita. Assim o download prossegue p/ /dv -> /c/. */
            if (g_override_reg_internal_pki && out_hdr != nullptr && log_result == 0x167b) {
                out_hdr->result = 0x0;
                ::ams::log::LogFormatImpl("SSL OVERRIDE obj=%u cmd=%u 0x167b -> 0x0 (b1 fix)\n", log_obj_id, log_cmd_id);
            }
            /* NPShop FIX b2: verificação de cert do SERVIDOR na webview da eShop. O web applet
             * (offlineweb 0100000000001042) importa a PRÓPRIA bundle de CA via ImportServerPki
             * (cmd 4, ~10 CAs) — NÃO usa a CertStore que a gente injeta. Então o cert do savanna
             * (assinado pela nossa CA) NÃO verifica -> DoHandshake (cmd 9) retorna erro de cert
             * -> a webview mostra 2811-1819. Como TODOS os hosts são NOSSOS, forçamos o handshake
             * a SUCESSO: no cmd 9, qualquer result que NÃO seja 0x0 (done) nem 0x1987b (WouldBlock)
             * é erro de verificação -> 0x0. (0x0/0x1987b são o fluxo normal não-bloqueante.) */
            /* NPShop: DoHandshake aparece em cmd 9 (contexto user) E cmd 8 (contexto do
             * game server .s.n.srv — obj=3 MK8D). O b2 original só pegava cmd 9, então o
             * handshake do game server morria em 0x19e7b (2123-0207 cert-verify) sem nunca
             * mandar WRITE. Estendido p/ cmd 8 -> força o handshake a passar p/ o cliente
             * MANDAR o 1o request (revela o protocolo do game server no SSL WRITE). 0x1987b
             * (WouldBlock) e 0x167b (b1, tratado acima) seguem intactos. */
            else if (g_override_reg_internal_pki && out_hdr != nullptr && (log_cmd_id == 9 || log_cmd_id == 8)
                     && log_result != 0x0 && log_result != 0x1987b && log_result != 0x167b && log_dom_type == 1) {
                ::ams::log::LogFormatImpl("SSL OVERRIDE obj=%u cmd=%u 0x%x -> 0x0 (b2 cert-verify)\n", log_obj_id, log_cmd_id, log_result);
                out_hdr->result = 0x0;
            }
            /* PERF: o log per-operação (grava no SD a CADA IPC ssl) é o gargalo da rede — só
             * quando verbose. O override b1/b2 acima já rodou independente disto. */
            if (g_verbose_log) {
                /* Só o result-code, lido do buffer de resposta do PRÓPRIO mitm (seguro). O log
                 * "SSL RESP" (que dereferenciava o recv buffer de app) foi REMOVIDO — mesma
                 * classe de crash do DIAG/HOST/WRITE. */
                ::ams::log::LogFormatImpl("SSL obj=%u cmd=%u -> result=0x%x (domtype=%u)\n",
                                          log_obj_id, log_cmd_id, log_result, log_dom_type);
            }
        }

        /* Parse, to ensure we catch any copy handles and close them. */
        {
            const auto response = hipcParseResponse(message_buffer);
            if (response.num_copy_handles) {
                ctx.handles_to_close->num_handles = response.num_copy_handles;
                for (size_t i = 0; i < response.num_copy_handles; i++) {
                    ctx.handles_to_close->handles[i] = response.copy_handles[i];
                }
            }
        }

        R_SUCCEED();
    }
    #endif

    void ServerSessionManager::DestroySession(ServerSession *session) {
        /* Destroy object. */
        std::destroy_at(session);

        /* Free object memory. */
        this->FreeSession(session);
    }

    void ServerSessionManager::CloseSessionImpl(ServerSession *session) {
        const auto session_handle = session->m_session_handle;
        os::FinalizeMultiWaitHolder(session);
        this->DestroySession(session);
        os::CloseNativeHandle(session_handle);
    }

    Result ServerSessionManager::RegisterSessionImpl(ServerSession *session_memory, os::NativeHandle session_handle, cmif::ServiceObjectHolder &&obj) {
        /* Create session object. */
        std::construct_at(session_memory, session_handle, std::forward<cmif::ServiceObjectHolder>(obj));

        /* Assign session resources. */
        session_memory->m_pointer_buffer = this->GetSessionPointerBuffer(session_memory);
        session_memory->m_saved_message  = this->GetSessionSavedMessageBuffer(session_memory);

        /* Register to wait list. */
        this->RegisterServerSessionToWait(session_memory);
        R_SUCCEED();
    }

    Result ServerSessionManager::AcceptSessionImpl(ServerSession *session_memory, os::NativeHandle port_handle, cmif::ServiceObjectHolder &&obj) {
        /* Create session handle. */
        os::NativeHandle session_handle;
        #if defined(ATMOSPHERE_OS_HORIZON)
        R_TRY(svc::AcceptSession(std::addressof(session_handle), port_handle));
        #else
        AMS_UNUSED(port_handle);
        AMS_ABORT("TODO");
        #endif

        auto session_guard = SCOPE_GUARD { os::CloseNativeHandle(session_handle); };

        /* Register session. */
        R_TRY(this->RegisterSessionImpl(session_memory, session_handle, std::forward<cmif::ServiceObjectHolder>(obj)));

        session_guard.Cancel();
        R_SUCCEED();
    }

    #if AMS_SF_MITM_SUPPORTED
    Result ServerSessionManager::RegisterMitmSessionImpl(ServerSession *session_memory, os::NativeHandle mitm_session_handle, cmif::ServiceObjectHolder &&obj, std::shared_ptr<::Service> &&fsrv) {
        AMS_ABORT_UNLESS(ServerManagerBase::CanAnyManageMitmServers());

        /* Create session object. */
        std::construct_at(session_memory, mitm_session_handle, std::forward<cmif::ServiceObjectHolder>(obj), std::forward<std::shared_ptr<::Service>>(fsrv));

        /* Assign session resources. */
        session_memory->m_pointer_buffer = this->GetSessionPointerBuffer(session_memory);
        session_memory->m_saved_message  = this->GetSessionSavedMessageBuffer(session_memory);

        /* Validate session pointer buffer. */
        AMS_ABORT_UNLESS(session_memory->m_pointer_buffer.GetSize() >= util::GetReference(session_memory->m_forward_service)->pointer_buffer_size);
        session_memory->m_pointer_buffer = cmif::PointerAndSize(session_memory->m_pointer_buffer.GetAddress(), util::GetReference(session_memory->m_forward_service)->pointer_buffer_size);

        /* Register to wait list. */
        this->RegisterServerSessionToWait(session_memory);
        R_SUCCEED();
    }

    Result ServerSessionManager::AcceptMitmSessionImpl(ServerSession *session_memory, os::NativeHandle mitm_port_handle, cmif::ServiceObjectHolder &&obj, std::shared_ptr<::Service> &&fsrv) {
        AMS_ABORT_UNLESS(ServerManagerBase::CanAnyManageMitmServers());

        /* Create session handle. */
        os::NativeHandle mitm_session_handle;
        R_TRY(svc::AcceptSession(std::addressof(mitm_session_handle), mitm_port_handle));

        auto session_guard = SCOPE_GUARD { os::CloseNativeHandle(mitm_session_handle); };

        /* Register session. */
        R_TRY(this->RegisterMitmSessionImpl(session_memory, mitm_session_handle, std::forward<cmif::ServiceObjectHolder>(obj), std::forward<std::shared_ptr<::Service>>(fsrv)));

        session_guard.Cancel();
        R_SUCCEED();
    }
    #endif

    Result ServerSessionManager::RegisterSession(os::NativeHandle session_handle, cmif::ServiceObjectHolder &&obj) {
        /* We don't actually care about what happens to the session. It'll get linked. */
        ServerSession *session_ptr = nullptr;
        R_RETURN(this->RegisterSession(std::addressof(session_ptr), session_handle, std::forward<cmif::ServiceObjectHolder>(obj)));
    }

    Result ServerSessionManager::AcceptSession(os::NativeHandle port_handle, cmif::ServiceObjectHolder &&obj) {
        /* We don't actually care about what happens to the session. It'll get linked. */
        ServerSession *session_ptr = nullptr;
        R_RETURN(this->AcceptSession(std::addressof(session_ptr), port_handle, std::forward<cmif::ServiceObjectHolder>(obj)));
    }

    #if AMS_SF_MITM_SUPPORTED
    Result ServerSessionManager::RegisterMitmSession(os::NativeHandle mitm_session_handle, cmif::ServiceObjectHolder &&obj, std::shared_ptr<::Service> &&fsrv) {
        /* We don't actually care about what happens to the session. It'll get linked. */
        ServerSession *session_ptr = nullptr;
        R_RETURN(this->RegisterMitmSession(std::addressof(session_ptr), mitm_session_handle, std::forward<cmif::ServiceObjectHolder>(obj), std::forward<std::shared_ptr<::Service>>(fsrv)));
    }

    Result ServerSessionManager::AcceptMitmSession(os::NativeHandle mitm_port_handle, cmif::ServiceObjectHolder &&obj, std::shared_ptr<::Service> &&fsrv) {
        /* We don't actually care about what happens to the session. It'll get linked. */
        ServerSession *session_ptr = nullptr;
        R_RETURN(this->AcceptMitmSession(std::addressof(session_ptr), mitm_port_handle, std::forward<cmif::ServiceObjectHolder>(obj), std::forward<std::shared_ptr<::Service>>(fsrv)));
    }
    #endif

    Result ServerSessionManager::ReceiveRequestImpl(ServerSession *session, const cmif::PointerAndSize &message) {
        const cmif::PointerAndSize &pointer_buffer = session->m_pointer_buffer;

        /* If the receive list is odd, we may need to receive repeatedly. */
        while (true) {
            if (pointer_buffer.GetPointer()) {
                hipcMakeRequestInline(message.GetPointer(),
                    .type = CmifCommandType_Invalid,
                    .num_recv_statics = HIPC_AUTO_RECV_STATIC,
                ).recv_list[0] = hipcMakeRecvStatic(pointer_buffer.GetPointer(), pointer_buffer.GetSize());
            } else {
                hipcMakeRequestInline(message.GetPointer(),
                    .type = CmifCommandType_Invalid,
                );
            }
            hipc::ReceiveResult recv_result;
            R_TRY(hipc::Receive(std::addressof(recv_result), session->m_session_handle, message));
            switch (recv_result) {
                case hipc::ReceiveResult::Success:
                    session->m_is_closed = false;
                    R_SUCCEED();
                case hipc::ReceiveResult::Closed:
                    session->m_is_closed = true;
                    R_SUCCEED();
                case hipc::ReceiveResult::NeedsRetry:
                    continue;
                AMS_UNREACHABLE_DEFAULT_CASE();
            }
        }
    }

    namespace {

        ALWAYS_INLINE u32 GetCmifCommandType(const cmif::PointerAndSize &message) {
            HipcHeader hdr = {};
            __builtin_memcpy(std::addressof(hdr), message.GetPointer(), sizeof(hdr));
            return hdr.type;
        }

    }

    Result ServerSessionManager::ProcessRequest(ServerSession *session, const cmif::PointerAndSize &message) {
        if (session->m_is_closed) {
            this->CloseSessionImpl(session);
            R_SUCCEED();
        }

        switch (GetCmifCommandType(message)) {
            case CmifCommandType_Close:
            {
                this->CloseSessionImpl(session);
                R_SUCCEED();
            }
            default:
            {
                R_TRY_CATCH(this->ProcessRequestImpl(session, message, message)) {
                    R_CATCH_RETHROW(sf::impl::ResultRequestContextChanged) /* A meta message changing the request context has been sent. */
                    R_CATCH_ALL() {
                        /* All other results indicate something went very wrong. */
                        this->CloseSessionImpl(session);
                        R_SUCCEED();
                    }
                } R_END_TRY_CATCH;

                /* We succeeded, so we can process future messages on this session. */
                this->RegisterServerSessionToWait(session);
                R_SUCCEED();
            }
        }
    }

    Result ServerSessionManager::ProcessRequestImpl(ServerSession *session, const cmif::PointerAndSize &in_message, const cmif::PointerAndSize &out_message) {
        /* TODO: Inline context support, retrieve from raw data + 0xC. */
        const auto cmif_command_type = GetCmifCommandType(in_message);

        const auto GetInlineContext = [&]() -> cmif::InlineContext {
            cmif::InlineContext ret  = {};
            switch (cmif_command_type) {
                case CmifCommandType_RequestWithContext:
                case CmifCommandType_ControlWithContext:
                    if (in_message.GetSize() >= 0x10) {
                        static_assert(sizeof(cmif::InlineContext) == 4);
                        std::memcpy(std::addressof(ret), static_cast<u8 *>(in_message.GetPointer()) + 0xC, sizeof(ret));
                    }
                    break;
                default:
                    break;
            }
            return ret;
        };

        cmif::ScopedInlineContextChanger sicc(GetInlineContext());
        switch (cmif_command_type) {
            case CmifCommandType_Request:
            case CmifCommandType_RequestWithContext:
                R_RETURN(this->DispatchRequest(session->m_srv_obj_holder.Clone(), session, in_message, out_message));
            case CmifCommandType_Control:
            case CmifCommandType_ControlWithContext:
                R_RETURN(this->DispatchManagerRequest(session, in_message, out_message));
            default:
                R_THROW(sf::hipc::ResultUnknownCommandType());
        }
    }

    Result ServerSessionManager::DispatchManagerRequest(ServerSession *session, const cmif::PointerAndSize &in_message, const cmif::PointerAndSize &out_message) {
        /* This will get overridden by ... WithDomain class. */
        AMS_UNUSED(session, in_message, out_message);
        R_THROW(sf::ResultNotSupported());
    }

    Result ServerSessionManager::DispatchRequest(cmif::ServiceObjectHolder &&obj_holder, ServerSession *session, const cmif::PointerAndSize &in_message, const cmif::PointerAndSize &out_message) {
        /* Create request context. */
        cmif::HandlesToClose handles_to_close = {};
        cmif::ServiceDispatchContext dispatch_ctx = {
            .srv_obj = obj_holder.GetServiceObjectUnsafe(),
            .manager = this,
            .session = session,
            .processor = nullptr, /* Filled in by template implementations. */
            .handles_to_close = std::addressof(handles_to_close),
            .pointer_buffer = session->m_pointer_buffer,
            .in_message_buffer = in_message,
            .out_message_buffer = out_message,
            .request = hipcParseRequest(in_message.GetPointer()),
        };

        /* Validate message sizes. */
        const uintptr_t in_message_buffer_end = in_message.GetAddress() + in_message.GetSize();
        const uintptr_t in_raw_addr = reinterpret_cast<uintptr_t>(dispatch_ctx.request.data.data_words);
        const size_t in_raw_size = dispatch_ctx.request.meta.num_data_words * sizeof(u32);
        /* Note: Nintendo does not validate this size before subtracting 0x10 from it. This is not exploitable. */
        R_UNLESS(in_raw_size >= 0x10, sf::hipc::ResultInvalidRequestSize());
        R_UNLESS(in_raw_addr + in_raw_size <= in_message_buffer_end, sf::hipc::ResultInvalidRequestSize());
        const size_t recv_list_size = dispatch_ctx.request.meta.num_recv_statics == HIPC_AUTO_RECV_STATIC ? 1 : dispatch_ctx.request.meta.num_recv_statics;
        const uintptr_t recv_list_end = reinterpret_cast<uintptr_t>(dispatch_ctx.request.data.recv_list + recv_list_size);
        R_UNLESS(recv_list_end <= in_message_buffer_end, sf::hipc::ResultInvalidRequestSize());

        /* CMIF has 0x10 of padding in raw data, and requires 0x10 alignment. */
        const cmif::PointerAndSize in_raw_data(util::AlignUp(in_raw_addr, 0x10), in_raw_size - 0x10);

        /* Invoke command handler. */
        R_TRY(obj_holder.ProcessMessage(dispatch_ctx, in_raw_data));

        /* Reply. */
        {
            ON_SCOPE_EXIT {
                for (size_t i = 0; i < handles_to_close.num_handles; i++) {
                    os::CloseNativeHandle(handles_to_close.handles[i]);
                }
            };
            R_TRY(hipc::Reply(session->m_session_handle, out_message));
        }

        R_SUCCEED();
    }


}
