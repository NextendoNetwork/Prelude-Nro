#pragma once
#include <stratosphere.hpp>

/* ssl-mitm: interface VAZIA — forwarda TODO comando ao ssl real (transparente).
 * cmd_id/object_id/result são logados no patch do ForwardRequest da
 * libstratosphere (sf_hipc_server_session_manager.cpp), lendo a TLS bruta.
 * Interface id 0x53534C00 = "SSL\0". */
#define AMS_SSL_MITM_INTERFACE_INFO(C, H)

AMS_SF_DEFINE_MITM_INTERFACE(ams::mitm::ssl, ISslMitmService, AMS_SSL_MITM_INTERFACE_INFO, 0x53534C00)
