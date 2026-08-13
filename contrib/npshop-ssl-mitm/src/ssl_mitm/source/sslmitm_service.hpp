#pragma once
#include <stratosphere.hpp>
#include "interfaces/issl.hpp"

namespace ams::mitm::ssl {

    /* nim program id — só logamos o ssl do nim (evita floodar com browser/eshop). */
    constexpr u64 NimProgramId = 0x0100000000000025ul;

    /* recarrega mitm_include.txt (whitelist de jogos, sem rebuild); chamado pelo poll thread do main. */
    void ReloadWhitelist();

    class SslMitmService : public sf::MitmServiceImplBase {
        public:
            SslMitmService(std::shared_ptr<::Service> &&s, const sm::MitmProcessInfo &c);
            static bool ShouldMitm(const sm::MitmProcessInfo &client_info);
            /* Sem overrides: interface vazia, tudo forwardado; log no dispatch patchado. */
    };
    static_assert(ams::mitm::ssl::IsISslMitmService<SslMitmService>);

}
