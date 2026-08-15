// Prelude — Nintendo Switch homebrew for the Nextendo Network.
// Copyright (C) 2026 Nextendo Network
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU Affero General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option) any
// later version.
//
// You should have received a copy of the GNU Affero General Public License along
// with this program. If not, see <https://www.gnu.org/licenses/>.

// ============================================================
//  Prelude — tables de traductions EN / ES / PT.
//  La langue courante est lue/écrite depuis la SD.
// ============================================================
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>

#include "lang.h"

Lang g_lang = LANG_EN;

#define LANG_PATH "sdmc:/switch/prelude_lang.txt"

// --- String tables -------------------------------------------------------
// Each column: EN, ES, PT, FR.
// Must match StringID enum order exactly.
// Brand names (Nextendo, Nintendo, Prelude) are kept as-is.
static const char *s_strings[STR_COUNT][4] = {
    // --- Picker screen ---
    [STR_TITLE_PRELUDE]      = { "Prelude", "Prelude", "Prelude", "Prelude" },
    [STR_MODE_ACTUAL_PREFIX] = { "Current mode: ", "Modo actual: ", "Modo atual: ", "Mode actuel : " },
    [STR_CURRENT_BADGE]      = { "CURRENT", "ACTUAL", "ATUAL", "ACTUEL" },
    [STR_SWITCH_BADGE]       = { "SWITCH HERE", "CAMBIAR AQUI", "MUDAR AQUI", "BASICULER ICI" },

    // --- Context descriptions ---
    [STR_DESC_NEXTENDO]      = { "Nextendo servers: custom online, icons, friends. Nextendo account required.",
                                 "Servidores Nextendo: online custom, iconos, amigos. Cuenta Nextendo requerida.",
                                 "Servidores Nextendo: online custom, iconos, amigos. Conta Nextendo necessaria.",
                                   "Serveurs Nextendo : online custom, icônes, amis. Compte Nextendo requis." },
    // Ne promet PAS "fonctionnement normal" : en emuMMC ce mode blanchit le PRODINFO,
    // donc les services en ligne n'authentifient plus. Le detail par type de console
    // est dit sur l'ecran de confirmation (STR_CONFIRM_RESTART_NINTENDO*).
    [STR_DESC_NINTENDO]      = { "Official Nintendo DNS restored. Telemetry stays blocked.",
                                 "DNS oficial de Nintendo restaurado. La telemetria sigue bloqueada.",
                                 "DNS oficial da Nintendo restaurado. A telemetria continua bloqueada.",
                                   "DNS officiel Nintendo restaure. La telemetrie reste bloquee." },
    [STR_DESC_S2]            = { "Downloads the Splatoon 2 rotation schedule and installs it into the game.",
                                 "Descarga el calendario de modos de Splatoon 2 y lo instala en el juego.",
                                 "Baixa o calendario de modos do Splatoon 2 e instala no jogo.",
                                   "Télécharge le calendrier des modes de Splatoon 2 et l'installe dans le jeu." },

    // --- Help lines ---
    [STR_HELP_MODE]          = { "A: switch mode       < >: change choice       X: sync time       B: quit",
                                 "A: cambiar modo       < >: cambiar eleccion       X: sinc. hora       B: salir",
                                 "A: mudar modo         < >: mudar escolha          X: sinc. hora       B: sair",
                                   "A : changer de mode       < > : changer de choix       X : heure       B : quitter" },
    [STR_HELP_S2]            = { "A: install schedule       < >: change choice       X: sync time       B: quit",
                                 "A: instalar horario       < >: cambiar eleccion       X: sinc. hora       B: salir",
                                 "A: instalar cronograma    < >: mudar escolha          X: sinc. hora       B: sair",
                                   "A : installer le planning       < > : changer de choix       X : heure       B : quitter" },

    // --- Update banner ---
    [STR_UPDATE_BANNER]      = { "MANDATORY update (v%d.%d.%d)   -   press Y to install",
                                 "Actualizacion OBLIGATORIA (v%d)   -   presiona Y para instalar",
                                 "Atualizacao OBRIGATORIA (v%d)   -   pressione Y para instalar",
                                   "Mise à jour OBLIGATOIRE (v%d)   -   appuie sur Y pour installer" },

    // --- Confirm screen ---
    [STR_CONFIRM_NEXTENDO]   = { "Switch to NEXTENDO mode?",
                                 "Pasar a modo NEXTENDO?",
                                 "Mudar para o modo NEXTENDO?",
                                   "Passer en mode NEXTENDO ?" },
    [STR_CONFIRM_NINTENDO]   = { "Switch to NINTENDO mode?",
                                 "Pasar a modo NINTENDO?",
                                 "Mudar para o modo NINTENDO?",
                                   "Passer en mode NINTENDO ?" },
    [STR_CONFIRM_REBOOT]     = { "The console will REBOOT now.",
                                 "La consola va a REINICIAR ahora.",
                                 "O console vai REINICIAR agora.",
                                   "La console va REDÉMARRER maintenant." },
    [STR_CONFIRM_RESTART_NEXTENDO] = { "After reboot: connected to Nextendo Network servers.",
                                       "Al reiniciar: conectado a los servidores Nextendo Network.",
                                       "Ao reiniciar: conectado aos servidores Nextendo Network.",
                                   "Au redémarrage : connecté aux serveurs Nextendo Network." },
    // Console SANS emuMMC : le PRODINFO reste le vrai (blank_prodinfo_emummc n'a aucun
    // effet en sysNAND), donc le retour aux serveurs officiels est REEL — eShop compris.
    // C'est aussi le cas a risque, d'ou l'avertissement STR_WARN_* affiche juste dessous.
    [STR_CONFIRM_RESTART_NINTENDO] = { "After reboot: back to official Nintendo servers.",
                                       "Al reiniciar: vuelta a los servidores oficiales Nintendo.",
                                       "Ao reiniciar: volta aos servidores oficiais Nintendo.",
                                   "Au redémarrage : retour aux serveurs officiels Nintendo." },
    // Console AVEC emuMMC : ce mode pose blank_prodinfo_emummc=1, l'identite de l'appareil
    // est blanchie et les services en ligne ne s'authentifient plus (ni eShop ni jeu en
    // ligne). C'est VOULU (c'est la protection anti-ban), mais l'ancien texte promettait
    // un "fonctionnement normal" qui n'arrivait jamais.
    // Tenu sous ~65 caracteres : la carte fait 820 px et le texte est centre sur UNE
    // ligne a 22 px. La plus longue chaine du design actuel en fait 63.
    [STR_CONFIRM_RESTART_NINTENDO_EMU] = { "After reboot: official DNS. Online services will not work.",
                                           "Al reiniciar: DNS oficial. Los servicios en linea no iran.",
                                           "Ao reiniciar: DNS oficial. Os servicos online nao irao.",
                                   "Au redémarrage : DNS officiel. Les services en ligne seront HS." },
    [STR_CONFIRM_CLOSE_GAMES] = { "Close any running games before confirming.",
                                  "Cierra cualquier juego abierto antes de confirmar.",
                                  "Feche qualquer jogo aberto antes de confirmar.",
                                   "Ferme tout jeu en cours avant de confirmer." },
    [STR_CONFIRM_A]          = { "A: Confirm", "A: Confirmar", "A: Confirmar", "A : Confirmer" },
    [STR_CONFIRM_B]          = { "B: Cancel", "B: Cancelar", "B: Cancelar", "B : Annuler" },

    // --- No-emuMMC warnings ---
    [STR_WARN_TITLE]         = { "WARNING: this console has no emuMMC.",
                                 "ATENCION: esta consola no tiene emuMMC.",
                                 "ATENCAO: este console nao tem emuMMC.",
                                   "ATTENTION : cette console n'a pas d'emuMMC." },
    [STR_WARN_LINE1]         = { "CFW runs on internal memory with your real console ID.",
                                 "El CFW funciona en la memoria interna con tu identificador real.",
                                 "O CFW roda na memoria interna com seu ID real do console.",
                                   "Le CFW tourne sur la mémoire interne avec ton vrai identifiant console." },
    [STR_WARN_LINE2]         = { "No identity protection without breaking eShop.",
                                 "No es posible proteger la identidad sin romper eShop.",
                                 "Nao e possivel proteger a identidade sem quebrar o eShop.",
                                   "Aucune protection d'identité possible sans casser l'eShop." },
    [STR_WARN_LINE3]         = { "Going ONLINE on real Nintendo remains at your own risk.",
                                 "Ir EN LINEA al Nintendo real sigue siendo bajo tu riesgo.",
                                 "Ir ONLINE no Nintendo real permanece sob seu risco.",
                                   "Aller EN LIGNE sur le vrai Nintendo reste à tes risques." },
    [STR_WARN_LINE4]         = { "(Telemetry is still blocked.)",
                                 "(La telemetria sigue bloqueada.)",
                                 "(A telemetria continua bloqueada.)",
                                   "(La télémétrie reste bloquée.)" },

    // --- S2 bar (picker screen) ---
    [STR_S2_BAR]             = { "Splatoon 2   -   Install online schedule",
                                 "Splatoon 2   -   Instalar planning en linea",
                                 "Splatoon 2   -   Instalar cronograma online",
                                   "Splatoon 2   -   Installer le planning en ligne" },

    // --- S2 info screen ---
    [STR_S2_TITLE]           = { "Splatoon 2 online schedule",
                                 "Planning en linea Splatoon 2",
                                 "Cronograma online Splatoon 2",
                                   "Planning en ligne Splatoon 2" },
    [STR_S2_DESC1]           = { "Downloads the rotation schedule (Turf War, Salmon Run, Festivals)",
                                 "Descarga el calendario de modos (Guerra Territorial, Salmon Run, Festivales)",
                                 "Baixa o calendario de modos (Guerra Territorial, Salmon Run, Festivais)",
                                   "Télécharge le calendrier des modes (Guerre Territoriale, Salmon Run, Festivals)" },
    [STR_S2_DESC2]           = { "from the Nextendo servers and installs it into Splatoon 2.",
                                 "desde los servidores Nextendo y lo instala en Splatoon 2.",
                                 "dos servidores Nextendo e instala no Splatoon 2.",
                                   "depuis les serveurs Nextendo et l'installe dans Splatoon 2." },
    [STR_S2_DESC3]           = { "Launch Splatoon 2 at least once first. No reboot required.",
                                 "Inicia Splatoon 2 al menos una vez antes. No requiere reinicio.",
                                 "Inicie Splatoon 2 pelo menos uma vez antes. Nao requer reinicializacao.",
                                   "Lance Splatoon 2 au moins une fois avant. Aucun redémarrage requis." },
    [STR_S2_DESC4]           = { "Each installation REPLACES the previous schedule.",
                                 "Cada instalacion REEMPLAZA el planning anterior.",
                                 "Cada instalacao SUBSTITUI o cronograma anterior.",
                                   "Chaque installation REMPLACE le planning précédent." },
    [STR_S2_A]               = { "A: Install", "A: Instalar", "A: Instalar", "A : Installer" },
    [STR_S2_B]               = { "B: Back", "B: Volver", "B: Voltar", "B : Retour" },

    // --- Progress screen ---
    [STR_PROGRESS_TITLE]     = { "Installation", "Instalacion", "Instalacao", "Installation" },
    [STR_PROGRESS_WAIT]      = { "Please wait...", "Espere por favor...", "Aguarde por favor...", "Patiente..." },

    // --- Result screen ---
    [STR_RESULT_RETURN]      = { "A / B: Return", "A / B: Volver", "A / B: Voltar", "A / B : Retour" },

    // --- Status messages (main.c) ---
    [STR_STATUS_NEXTENDO_OK]   = { "NEXTENDO mode applied  -  rebooting...",
                                    "Modo NEXTENDO aplicado  -  reiniciando...",
                                    "Modo NEXTENDO aplicado  -  reiniciando...",
                                    "Mode NEXTENDO appliqué  -  redémarrage..." },
    [STR_STATUS_NINTENDO_OK]   = { "NINTENDO mode applied  -  rebooting...",
                                   "Modo NINTENDO aplicado  -  reiniciando...",
                                   "Modo NINTENDO aplicado  -  reiniciando...",
                                   "Mode NINTENDO appliqué  -  redémarrage..." },
    [STR_STATUS_REBOOT_FAIL]   = { "Reboot failed",
                                   "Error al reiniciar",
                                   "Falha ao reiniciar",
                                   "Échec du redémarrage" },
    [STR_STATUS_SD_ERROR]      = { "ERROR: cannot write to SD card",
                                   "ERROR: no se puede escribir en la SD",
                                   "ERRO: nao e possivel escrever no cartao SD",
                                   "ERREUR : écriture sur la SD impossible" },
    [STR_STATUS_DOWNLOAD_SCHEDULE] = { "Downloading and installing schedule...",
                                       "Descargando e instalando planning...",
                                       "Baixando e instalando cronograma...",
                                   "Téléchargement et installation du planning..." },
    [STR_STATUS_SCHEDULE_OK]   = { "Schedule installed",
                                   "Planning instalado",
                                   "Cronograma instalado",
                                   "Planning installé" },
    [STR_STATUS_SCHEDULE_OK_DESC]  = { "Old schedule replaced. Relaunch Splatoon 2 to apply.",
                                       "Planning anterior reemplazado. Reinicia Splatoon 2 para aplicar.",
                                       "Cronograma anterior substituido. Reinicie Splatoon 2 para aplicar.",
                                   "Ancien planning remplacé. Relance Splatoon 2 pour l'appliquer." },
    [STR_STATUS_NO_SCHEDULE]   = { "No schedule",
                                   "Sin planning",
                                   "Sem cronograma",
                                   "Aucun planning" },
    [STR_STATUS_NO_SCHEDULE_DESC]  = { "Nothing published yet.",
                                       "Nada publicado aun.",
                                       "Nada publicado ainda.",
                                   "Rien de publié pour le moment." },
    [STR_STATUS_MOUNT_FAIL]    = { "Save data not found",
                                   "Datos de guardado no encontrados",
                                   "Dados salvos nao encontrados",
                                   "Sauvegarde introuvable" },
    [STR_STATUS_MOUNT_FAIL_DESC]   = { "Launch Splatoon 2 once, then try again.",
                                       "Inicia Splatoon 2 una vez, luego intenta de nuevo.",
                                       "Inicie Splatoon 2 uma vez, entao tente novamente.",
                                   "Lance Splatoon 2 une fois, puis réessaie." },
    [STR_STATUS_NET_FAIL]      = { "Network error",
                                   "Error de red",
                                   "Erro de rede",
                                   "Erreur réseau" },
    [STR_STATUS_NET_FAIL_DESC] = { "Download failed (check connection / mode).",
                                   "Fallo la descarga (verifica conexion / modo).",
                                   "Falha no download (verifique conexao / modo).",
                                   "Téléchargement impossible (vérifie la connexion / le mode)." },
    [STR_STATUS_WRITE_FAIL]    = { "Write error",
                                   "Error de escritura",
                                   "Erro de gravacao",
                                   "Erreur d'écriture" },
    [STR_STATUS_WRITE_FAIL_DESC]   = { "Cannot write to save data.",
                                       "No se puede escribir en los datos de guardado.",
                                       "Nao e possivel gravar nos dados salvos.",
                                   "Écriture dans la sauvegarde impossible." },
    [STR_STATUS_NET_CONNECT]   = { "Server unreachable",
                                   "Servidor inaccesible",
                                   "Servidor inacessivel",
                                   "Serveur inaccessible" },
    [STR_STATUS_NET_CONNECT_DESC] = { "Cannot connect to server (check internet / mode).",
                                      "No se puede conectar al servidor (verifica internet / modo).",
                                      "Nao e possivel conectar ao servidor (verifique internet / modo).",
                                   "Impossible de contacter le serveur (vérifie connexion / mode)." },
    [STR_STATUS_NET_TIMEOUT]   = { "Connection timeout",
                                   "Tiempo de conexion agotado",
                                   "Tempo de conexao esgotado",
                                   "Délai de connexion dépassé" },
    [STR_STATUS_NET_TIMEOUT_DESC] = { "Server took too long to respond. Try again.",
                                      "El servidor tardo demasiado en responder. Intenta de nuevo.",
                                      "O servidor demorou muito para responder. Tente novamente.",
                                   "Le serveur a mis trop de temps à répondre. Réessaie." },
    [STR_STATUS_NET_HTTP_ERR]  = { "Server error",
                                   "Error del servidor",
                                   "Erro do servidor",
                                   "Erreur du serveur" },
    [STR_STATUS_NET_HTTP_ERR_DESC] = { "Server returned an unexpected response.",
                                       "El servidor devolvio una respuesta inesperada.",
                                       "O servidor retornou uma resposta inesperada.",
                                   "Le serveur a renvoyé une réponse inattendue." },
    [STR_STATUS_DOWNLOAD_UPDATE]   = { "Downloading update...",
                                       "Descargando actualizacion...",
                                       "Baixando atualizacao...",
                                   "Téléchargement de la mise à jour..." },
    [STR_STATUS_UPDATE_OK]     = { "Update installed",
                                   "Actualizacion instalada",
                                   "Atualizacao instalada",
                                   "Mise à jour installée" },
    [STR_STATUS_UPDATE_OK_DESC]    = { "CLOSE and relaunch Prelude to apply v%d.%d.%d.",
                                        "CIERRA y reinicia Prelude para aplicar v%d.",
                                        "FECHE e reinicie o Prelude para aplicar v%d.",
                                   "FERME et relance Prelude pour appliquer la v%d." },
    [STR_STATUS_UPDATE_SIZE_FAIL]  = { "Download corrupted",
                                       "Descarga corrupta",
                                       "Download corrompido",
                                   "Téléchargement corrompu" },
    [STR_STATUS_UPDATE_SIZE_FAIL_DESC] = { "Unexpected size. Try again.",
                                           "Tamano inesperado. Intenta de nuevo.",
                                           "Tamanho inesperado. Tente novamente.",
                                   "Taille inattendue. Réessaie." },
    [STR_STATUS_UPDATE_WRITE_FAIL] = { "Write failed",
                                       "Error de escritura",
                                       "Falha ao escrever",
                                   "Échec d'écriture" },
    [STR_STATUS_UPDATE_WRITE_FAIL_DESC] = { "Cannot write to SD card.",
                                            "No se puede escribir en la SD.",
                                            "Nao e possivel escrever no cartao SD.",
                                   "Impossible d'écrire sur la carte SD." },
    [STR_STATUS_UPDATE_NET_FAIL]   = { "Network error",
                                       "Error de red",
                                       "Erro de rede",
                                   "Erreur réseau" },
    [STR_STATUS_UPDATE_NET_FAIL_DESC] = { "Download failed.",
                                          "Fallo la descarga.",
                                          "Falha no download.",
                                   "Téléchargement impossible." },

    // --- Language menu ---
    [STR_LANG_TITLE]        = { "Language", "Idioma", "Idioma", "Langue" },
    [STR_LANG_EN]           = { "English", "English", "Ingles", "Anglais" },
    [STR_LANG_ES]           = { "Spanish", "Espanol", "Espanhol", "Espagnol" },
    [STR_LANG_PT]           = { "Portuguese", "Portugues", "Portugues", "Portuguais" },
    [STR_LANG_FR]           = { "French", "Frances", "Frances", "Francais" },
    [STR_LANG_DEFAULT]      = { "(Default)", "(Defecto)", "(Padrao)", "(Défaut)" },
    [STR_LANG_A_SELECT]     = { "A: Select", "A: Elegir", "A: Selecionar", "A : Choisir" },
    [STR_LANG_B_BACK]       = { "B: Back", "B: Volver", "B: Voltar", "B : Retour" },

    // --- Update confirmation ---
    [STR_UPD_CONFIRM_TITLE]    = { "Update available",
                                   "Actualizacion disponible",
                                   "Atualizacao disponivel",
                                   "Mise à jour disponible" },
    [STR_UPD_CONFIRM_VERSION]  = { "New version: v%d.%d.%d",
                                   "Nueva version: build %d",
                                   "Nova versao: build %d",
                                   "Nouvelle version : build %d" },
    [STR_UPD_CONFIRM_DESC]    = { "Prelude will download and replace itself.",
                                  "Prelude descargara y se reemplazara a si mismo.",
                                  "Prelude baixara e substituira a si mesmo.",
                                  "Prelude va se télécharger et se remplacer." },
    [STR_UPD_CONFIRM_A]       = { "A: Download and install",
                                  "A: Descargar e instalar",
                                  "A: Baixar e instalar",
                                  "A: Télécharger et installer" },
    [STR_UPD_CONFIRM_B]       = { "B: Cancel",
                                   "B: Cancelar",
                                   "B: Cancelar",
                                   "B: Annuler" },

    // --- Server switch toast ---
    [STR_TOAST_SERVER_DEFAULT] = { "Server: VPS (51.178.29.194)",
                                   "Servidor: VPS (51.178.29.194)",
                                   "Servidor: VPS (51.178.29.194)",
                                   "Serveur : VPS (51.178.29.194)" },
    [STR_TOAST_SERVER_ALT]     = { "Server: Local (3.135.232.168)",
                                   "Servidor: Local (3.135.232.168)",
                                   "Servidor: Local (3.135.232.168)",
                                   "Serveur : Local (3.135.232.168)" },

    // --- Flag bar ---
    [STR_FLAG_BAR]      = { "MK8D   -   Country Flag",
                            "MK8D   -   Bandera de pais",
                            "MK8D   -   Bandeira do pais",
                            "MK8D   -   Drapeau du pays" },
    [STR_FLAG_BAR_SET]  = { "MK8D   -   Flag: %s",
                            "MK8D   -   Bandera: %s",
                            "MK8D   -   Bandeira: %s",
                            "MK8D   -   Drapeau : %s" },
    [STR_DESC_FLAG]     = { "Install a country flag for MK8D online play. Other players will see it.",
                            "Instala una bandera de pais para MK8D online. Los demas jugadores la veran.",
                            "Instala uma bandeira para MK8D online. Os outros jogadores vao ve-la.",
                            "Installe un drapeau national pour MK8D en ligne. Les autres joueurs le voient." },
    [STR_HELP_FLAG]     = { "A: open   < >: change   X: sync time   B: quit",
                            "A: abrir   < >: cambiar   X: sinc. hora   B: salir",
                            "A: abrir   < >: mudar   X: sinc. hora   B: sair",
                            "A : ouvrir   < > : changer   X : heure   B : quitter" },

    // --- Flag menu ---
    [STR_FLAG_MENU_TITLE] = { "MK8D Country Flag", "Bandera de pais MK8D",
                              "Bandeira do pais MK8D", "Drapeau du pays MK8D" },
    [STR_FLAG_NONE]       = { "(none installed)", "(ninguna instalada)",
                              "(nenhuma instalada)", "(aucun installe)" },
    [STR_FLAG_INSTALLED]  = { "INSTALLED", "INSTALADA", "INSTALADA", "INSTALLE" },
    [STR_FLAG_A]          = { "A: Install", "A: Instalar", "A: Instalar", "A : Installer" },
    [STR_FLAG_B]          = { "B: Back", "B: Volver", "B: Voltar", "B : Retour" },

    // --- Flag progress / result ---
    [STR_STATUS_DOWNLOAD_FLAG]      = { "Downloading flag...",
                                        "Descargando bandera...",
                                        "Baixando bandeira...",
                                        "Telechargement du drapeau..." },
    [STR_STATUS_FLAG_OK]            = { "Flag installed",
                                        "Bandera instalada",
                                        "Bandeira instalada",
                                        "Drapeau installe" },
    [STR_STATUS_FLAG_OK_DESC]       = { "Restart MK8D to apply the new country.",
                                        "Reinicia MK8D para aplicar el nuevo pais.",
                                        "Reinicie MK8D para aplicar o novo pais.",
                                        "Redemarrer MK8D pour appliquer le nouveau pays." },
    [STR_STATUS_FLAG_NET_FAIL]      = { "Network error",
                                        "Error de red",
                                        "Erro de rede",
                                        "Erreur reseau" },
    [STR_STATUS_FLAG_NET_FAIL_DESC] = { "Could not download flag. Check connection.",
                                        "No se pudo descargar la bandera. Verifica la conexion.",
                                        "Nao foi possivel baixar a bandeira. Verifique a conexao.",
                                        "Impossible de telecharger le drapeau. Verifier la connexion." },
    [STR_STATUS_FLAG_WRITE_FAIL]      = { "Write error",
                                          "Error de escritura",
                                          "Erro de gravacao",
                                          "Erreur d'ecriture" },
    [STR_STATUS_FLAG_WRITE_FAIL_DESC] = { "Cannot write to SD card.",
                                          "No se puede escribir en la SD.",
                                          "Nao e possivel gravar no cartao SD.",
                                          "Impossible d'ecrire sur la carte SD." },

    // --- Time sync progress / result ---
    [STR_STATUS_SYNC_TIME]            = { "Synchronizing time...",
                                          "Sincronizando hora...",
                                          "Sincronizando hora...",
                                          "Synchronisation de l'heure..." },
    [STR_STATUS_TIME_OK]              = { "Time synchronized",
                                          "Hora sincronizada",
                                          "Hora sincronizada",
                                          "Heure synchronisee" },
    [STR_STATUS_TIME_OK_DESC]         = { "The console time has been successfully synchronized.",
                                          "La hora de la consola se ha sincronizado correctamente.",
                                          "A hora do console foi sincronizada com sucesso.",
                                          "L'heure de la console a ete synchronisee avec succes." },
    [STR_STATUS_TIME_FAIL]            = { "Synchronization error",
                                          "Error de sincronizacion",
                                          "Erro de sincronizacao",
                                          "Erreur de synchronisation" },
    [STR_STATUS_TIME_FAIL_DESC]       = { "Could not synchronize time (check connection).",
                                          "No se pudo sincronizar la hora (verifica la conexion).",
                                          "Nao foi possivel sincronizar a hora (verifique a conexao).",
                                          "Impossible de synchroniser l'heure (verifier la connexion)." },
};

// ============================================================

void lang_init(void) {
    FILE *f = fopen(LANG_PATH, "rb");
    if (!f) { g_lang = LANG_EN; return; }
    int c = fgetc(f);
    fclose(f);
    if (c == 'E') g_lang = LANG_EN;
    else if (c == 'S') g_lang = LANG_ES;
    else if (c == 'P') g_lang = LANG_PT;
    else if (c == 'F') g_lang = LANG_FR;
    else g_lang = LANG_EN;
}

void lang_save(void) {
    char ch = 'E';
    if (g_lang == LANG_ES) ch = 'S';
    else if (g_lang == LANG_PT) ch = 'P';
    else if (g_lang == LANG_FR) ch = 'F';
    // else LANG_EN -> 'E'
    FILE *f = fopen(LANG_PATH, "wb");
    if (!f) {
        mkdir("sdmc:/switch", 0777);
        f = fopen(LANG_PATH, "wb");
    }
    if (f) { fputc(ch, f); fclose(f); }
    fsdevCommitDevice("sdmc");
}

const char *lang_str(StringID id) {
    if (id < 0 || id >= STR_COUNT) return "?";
    if (g_lang < 0 || g_lang >= (int)(sizeof(s_strings[0]) / sizeof(s_strings[0][0])))
        g_lang = LANG_EN;
    return s_strings[id][g_lang];
}
