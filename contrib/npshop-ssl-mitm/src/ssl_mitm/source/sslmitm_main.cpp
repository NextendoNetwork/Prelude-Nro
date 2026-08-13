/*
 * NPShop ssl mitm — forwarda o serviço ssl (transparente) e LOGA cmd/obj/result
 * de cada comando IPC do nim, p/ achar qual operação ssl retorna 0x167b
 * (2123-0011) ao montar a conexão TLS de download. Base: ldn_mitm (GPLv2).
 */
#include <stratosphere.hpp>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <malloc.h>
#include <switch.h>

#include "sslmitm_service.hpp"
#include "debug.hpp"

/* Definido na libstratosphere patchada (sf_hipc_server_session_manager.cpp):
 * liga o log de cmd/obj/result no ForwardRequest só p/ este processo. */
namespace ams::sf::hipc { void SetForwardRequestLogging(bool enabled); void SetOverrideRegisterInternalPki(bool enabled); void SetForwardRequestVerboseLog(bool enabled); }

namespace ams {

    namespace {
        constexpr size_t MallocBufferSize = 1_MB;
        alignas(os::MemoryPageSize) constinit u8 g_malloc_buffer[MallocBufferSize];
    }

    namespace mitm {

        const s32 ThreadPriority = 6;
        const size_t TotalThreads = 2;
        const size_t NumExtraThreads = TotalThreads - 1;
        const size_t ThreadStackSize = 0x4000;

        alignas(os::MemoryPageSize) u8 g_thread_stack[ThreadStackSize];
        os::ThreadType g_thread;

        /* Toggle do log verboso EM RUNTIME (sem reboot): thread que faz polling do flag-file a
         * cada 2s e liga/desliga g_verbose_log. Criar/apagar
         * sdmc:/atmosphere/contents/4200000000000053/flags/verbose_ssl.flag reflete em ~2s. */
        os::ThreadType g_verbose_thread;
        alignas(os::MemoryPageSize) u8 g_verbose_stack[0x2000];
        void VerbosePollLoop(void *) {
            for (;;) {
                bool has = false;
                if (R_FAILED(fs::HasFile(&has, "sdmc:/atmosphere/contents/4200000000000053/flags/verbose_ssl.flag"))) { has = false; }
                sf::hipc::SetForwardRequestVerboseLog(has);
                SetLogging(has ? 1 : 0);  // FIX: liga TAMBÉM o log de arquivo (sdmc:/ssl_mitm.log)
                ssl::ReloadWhitelist();  // recarrega mitm_include.txt (whitelist de jogos, sem rebuild)
                os::SleepThread(TimeSpan::FromSeconds(2));
            }
        }

        alignas(0x40) constinit u8 g_heap_memory[128_KB];
        constinit lmem::HeapHandle g_heap_handle;
        constinit bool g_heap_initialized;
        constinit os::SdkMutex g_heap_init_mutex;

        lmem::HeapHandle GetHeapHandle() {
            if (AMS_UNLIKELY(!g_heap_initialized)) {
                std::scoped_lock lk(g_heap_init_mutex);
                if (AMS_LIKELY(!g_heap_initialized)) {
                    g_heap_handle = lmem::CreateExpHeap(g_heap_memory, sizeof(g_heap_memory), lmem::CreateOption_ThreadSafe);
                    g_heap_initialized = true;
                }
            }
            return g_heap_handle;
        }

        void *Allocate(size_t size)   { return lmem::AllocateFromExpHeap(GetHeapHandle(), size); }
        void  Deallocate(void *p, size_t size) { AMS_UNUSED(size); return lmem::FreeToExpHeap(GetHeapHandle(), p); }

        namespace {

            struct SslMitmManagerOptions {
                static constexpr size_t PointerBufferSize   = 0x1000;
                // SSBU (e outros jogos online pesados) abrem MUITO objeto SSL de domínio (ISslContext/
                // ISslConnection) — o pool é COMPARTILHADO (m_domain_entry_storages[MaxDomainObjects],
                // TOTAL entre todos os domínios) e o GetEntry(id) ABORTA se id > MaxDomainObjects. Crashes
                // reais (fatal AFE2 prog …0053): AllocateSpecificEntries E ConvertCurrentObjectToDomain->
                // RegisterObject (sf_cmif_domain_manager.cpp:150 / sf_hipc_server_domain_session_manager:92)
                // — os dois caem no MESMO pool. 0x100(256) estourava na hora; 0x800(2048) ainda estourou
                // sob carga do SSBU+matchmaking. Subo forte pra 0x2000(8192)+64 domínios. Só cresce .bss
                // estático (arquivo igual). Se AINDA estourar, o teto real é do serviço ssl da Nintendo.
                static constexpr size_t MaxDomains          = 0x40;
                static constexpr size_t MaxDomainObjects    = 0x2000;
                static constexpr bool   CanDeferInvokeRequest = false;
                static constexpr bool   CanManageMitmServers  = true;
            };

            constexpr size_t MaxSessions = 48;
            constexpr size_t NumPorts    = 2; /* ssl (user) + ssl:s (system, download do nim) */

            class ServerManager final : public sf::hipc::ServerManager<NumPorts, SslMitmManagerOptions, MaxSessions> {
                private:
                    virtual ams::Result OnNeedsToAccept(int port_index, Server *server) override;
            };

            ServerManager g_server_manager;

            Result ServerManager::OnNeedsToAccept(int port_index, Server *server) {
                AMS_UNUSED(port_index);
                std::shared_ptr<::Service> fsrv;
                sm::MitmProcessInfo client_info;
                server->AcknowledgeMitmSession(std::addressof(fsrv), std::addressof(client_info));
                return this->AcceptMitmImpl(
                    server,
                    sf::CreateSharedObjectEmplaced<mitm::ssl::ISslMitmService, mitm::ssl::SslMitmService>(decltype(fsrv)(fsrv), client_info),
                    fsrv);
            }

            alignas(os::MemoryPageSize) u8 g_extra_thread_stacks[NumExtraThreads][ThreadStackSize];
            os::ThreadType g_extra_threads[NumExtraThreads];

            void LoopServerThread(void *) { g_server_manager.LoopProcess(); }

            void ProcessForServerOnAllThreads(void *) {
                if constexpr (NumExtraThreads > 0) {
                    const s32 priority = os::GetThreadCurrentPriority(os::GetCurrentThread());
                    for (size_t i = 0; i < NumExtraThreads; i++) {
                        R_ABORT_UNLESS(os::CreateThread(g_extra_threads + i, LoopServerThread, nullptr, g_extra_thread_stacks[i], ThreadStackSize, priority));
                        os::SetThreadNamePointer(g_extra_threads + i, "ssl_mitm::Thread");
                    }
                    for (size_t i = 0; i < NumExtraThreads; i++) { os::StartThread(g_extra_threads + i); }
                }
                LoopServerThread(nullptr);
                if constexpr (NumExtraThreads > 0) {
                    for (size_t i = 0; i < NumExtraThreads; i++) { os::WaitThread(g_extra_threads + i); }
                }
            }
        }
    }

    namespace init {

        void InitializeSystemModule() {
            R_ABORT_UNLESS(sm::Initialize());
            fs::InitializeForSystem();
            fs::SetAllocator(mitm::Allocate, mitm::Deallocate);
            fs::SetEnabledAutoAbort(false);
            /* NPShop fatal-proof: o SD serve só p/ log (off por padrão). NUNCA abortar se
             * o mount falhar — SD lenta/ainda montando no boot2 NÃO pode fatalar o console.
             * Retry best-effort (~2s) e segue sem SD; o mitm funciona sem log. */
            for (int i = 0; i < 20 && R_FAILED(fs::MountSdCard("sdmc")); i++) {
                os::SleepThread(TimeSpan::FromMilliSeconds(100));
            }
        }

        void FinalizeSystemModule() { /* ... */ }

        void Startup() {
            init::InitializeAllocator(g_malloc_buffer, sizeof(g_malloc_buffer));
        }
    }

    void NORETURN Exit(int rc) { AMS_UNUSED(rc); AMS_ABORT("Exit called by immortal process"); }

    void Main() {
        /* Não abortar se o log falhar (boot2 cedo) — só queremos registrar o mitm. */
        log::Initialize();
        LogFormat("ssl_mitm main");

        /* Liga o log de cmd/obj/result no ForwardRequest (só neste processo). */
        sf::hipc::SetForwardRequestLogging(true);
        /* NPShop ESTAVEL: b1 LIGADO — flip do result IPC final 0x167b->0 (cmd 8
         * RegisterInternalPki). Imune a' cascata e NAO crasha (so' mexe na resposta IPC,
         * nao no 0x167b interno load-bearing). b2 (cert-verify) e' estatico no exefs. */
        sf::hipc::SetOverrideRegisterInternalPki(true);

        /* Log VERBOSO per-operação (grava no SD a cada IPC -> rede lenta). OFF por padrão. Um thread
         * faz polling do flag e liga/desliga EM RUNTIME (criar/apagar
         * sdmc:/atmosphere/contents/4200000000000053/flags/verbose_ssl.flag reflete em ~2s, sem reboot).
         * Os fixes b1/b2 rodam independente disto. */
        R_ABORT_UNLESS(os::CreateThread(&mitm::g_verbose_thread, mitm::VerbosePollLoop, nullptr,
                                        mitm::g_verbose_stack, sizeof(mitm::g_verbose_stack), mitm::ThreadPriority));
        os::SetThreadNamePointer(&mitm::g_verbose_thread, "ssl_mitm::VerbosePoll");
        os::StartThread(&mitm::g_verbose_thread);

        /* Registra ssl:s (system) — é o que o download do nim usa — e ssl (user).
         * ssl:s é o crítico; se algum falhar, loga e segue (não derruba o módulo).
         * Aborta apenas se AMBOS falharem. */
        const auto rc_sys  = mitm::g_server_manager.RegisterMitmServer<mitm::ssl::SslMitmService>(0, sm::ServiceName::Encode("ssl:s"));
        LogFormat("ssl:s mitm register -> 0x%x", rc_sys.GetValue());
        const auto rc_user = mitm::g_server_manager.RegisterMitmServer<mitm::ssl::SslMitmService>(1, sm::ServiceName::Encode("ssl"));
        LogFormat("ssl mitm register -> 0x%x", rc_user.GetValue());

        if (R_FAILED(rc_sys) && R_FAILED(rc_user)) {
            LogFormat("FATAL: could not register ssl nor ssl:s");
            R_ABORT_UNLESS(rc_sys);
        }

        R_ABORT_UNLESS(os::CreateThread(&mitm::g_thread, mitm::ProcessForServerOnAllThreads, nullptr,
                                        mitm::g_thread_stack, mitm::ThreadStackSize, mitm::ThreadPriority));
        os::SetThreadNamePointer(&mitm::g_thread, "ssl_mitm::MainThread");
        os::StartThread(&mitm::g_thread);
        os::WaitThread(&mitm::g_thread);
    }
}

void *operator new(size_t size)                          { return ams::mitm::Allocate(size); }
void *operator new(size_t size, const std::nothrow_t &)  { return ams::mitm::Allocate(size); }
void  operator delete(void *p)                           { return ams::mitm::Deallocate(p, 0); }
void  operator delete(void *p, size_t size)              { return ams::mitm::Deallocate(p, size); }
void *operator new[](size_t size)                        { return ams::mitm::Allocate(size); }
void *operator new[](size_t size, const std::nothrow_t &){ return ams::mitm::Allocate(size); }
void  operator delete[](void *p)                         { return ams::mitm::Deallocate(p, 0); }
void  operator delete[](void *p, size_t size)            { return ams::mitm::Deallocate(p, size); }
