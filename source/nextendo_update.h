// Prelude — Nintendo Switch homebrew for the Nextendo Network.
// Copyright (C) 2026 Nextendo Network
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU Affero General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option) any
// later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
// PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along
// with this program. If not, see <https://www.gnu.org/licenses/>.

// ============================================================
//  Nextendo .nro — auto-mise a jour (via le serveur Nextendo).
//  Au lancement, on interroge /api/nro/latest ; si une version plus recente
//  existe, on affiche un bandeau. L'utilisateur peut telecharger le nouveau
//  .nro et remplacer /switch/nextendo.nro (le .nro courant tourne depuis la RAM).
// ============================================================
#ifndef NEXTENDO_UPDATE_H
#define NEXTENDO_UPDATE_H
#include <switch.h>

// Version de CE build. A INCREMENTER a chaque release (le serveur renvoie la derniere).
// build 4 : mode Nintendo coupe network_mitm (fix eShop 2137-7403) + auto-provisioning
//           du stack cert-trust complet au 1er passage en mode Nextendo (zero install manuelle).
// build 5 : release Prelude v1 (rebrand du homebrew Nextendo -> Prelude ; MAJ desormais OBLIGATOIRE).
// build 6 : fix "impossible d'ecrire sur la carte SD" a la MAJ (rename() qui echoue sur
//           certaines cartes fatfs -> secours par ecriture directe + mkdir defensif).
// build 7 : ALIGNEMENT DES VERSIONS. La version NACP affichee dans hbmenu etait restee
//           "1.0.0" (jamais bumpee) alors que le build interne etait deja 6 -> confusion.
//           Desormais APP_VERSION (Makefile) = 1.0.N ou N = NEXTENDO_BUILD. Build 7 = 1.0.7.
// build 8 : PRODINFO dynamique par mode (emuMMC). Mode NEXTENDO -> blank_prodinfo_emummc=0
//           (vrai cert device -> fix du 2123-0011 qui frappait tous les emuMMC incognito ;
//           confine par DNS-MITM donc l'identite ne fuit jamais = safe). Mode NINTENDO ->
//           blank_prodinfo_emummc=1 (identite blanche -> anti-ban si online sur le vrai Nintendo
//           en emuMMC). Le sysNAND (blank_prodinfo_sysmmc) n'est JAMAIS touche.
// build 9 : (a) FIX NAT : l'IP nncs2 codee en dur (178.105.220.158) pointait sur l'ancienne
//           machine nncs2, desormais hors service -> Pia n'obtenait jamais la 2e sonde -> NAT
//           jamais complete -> MK8/S2 tombaient en 2618-201. Remplacee par le VPS OVH
//           164.132.111.120 qui fait tourner le meme responder. (b) PURGE des fichiers laisses par les anciens builds (network_mitm
//           4200000000000666, anciens emplacements du bundle CA navigateur, IPS d'un build-id
//           qui n'est plus cible) : appliquee DANS LES DEUX MODES, donc une console qui a un
//           vieux Prelude est nettoyee des le 1er lancement. En mode NINTENDO c'est critique :
//           couper dns_mitm ne suffisait pas, un network_mitm residuel continuait d'intercepter
//           ssl/ssl:s.
// build 10 : AUDIT DE SECURITE, mesure sur une vraie console. Quatre trous, tous constates :
//           (a) TELEMETRIE. Le mode Nintendo posait enable_dns_mitm=0 + add_defaults=0, soit les
//               deux opt-out du blocage de telemetrie natif d'Atmosphere ("By default, atmosphere
//               redirects resolution requests for official telemetry servers") : la console etait
//               MOINS protegee qu'une install d'origine, alors que c'est justement le mode ou elle
//               parle aux vrais serveurs. Desormais dns_mitm reste ACTIF avec add_defaults=1 ; nos
//               hosts etant supprimes, DNS.mitm retombe sur default.txt et bloque la telemetrie,
//               l'eShop continuant de marcher. Secours dns_mitm=0 si nos hosts resistent.
//           (b) CERT-TRUST. Les patches disable_ca_verification, la CA Nextendo injectee dans le
//               navigateur et rootCA.pem survivaient a la bascule -> en mode Nintendo la verif des
//               certificats restait desactivee et notre CA de confiance, face au VRAI Nintendo.
//               Le mode Nintendo les retire maintenant (removeTreeRomfs, miroir de l'install).
//           (c) FUITE D'IP. dns_mitm_debug.log gardait notre IP a chaque requete (608 Ko / 7507
//               lignes sur la carte de test), dns_mitm_startup.log la table complete des
//               redirections, et les .txt.bak les hosts en clair — alors que le .bak n'etait
//               JAMAIS relu. Tout est purge dans les deux modes.
//           (d) PRODINFO. blank_prodinfo_emummc est sans effet sur une console sans emuMMC (CFW
//               sur la memoire interne) : aucune protection n'y est possible sans casser l'eShop,
//               raison d'etre du mode Nintendo. On ne peut pas corriger, donc on previent :
//               l'ecran de confirmation le dit franchement (splGetConfig ExosphereEmummcType).
// build 11 : build de DIAGNOSTIC. Le build 10 gelait la console au moment de basculer en mode
//           NINTENDO : l'ecran s'affichait, A ne repondait plus, et la carte restait pourtant
//           intacte (donc apply_nintendo n'a rien commit). Impossible a reproduire (Ryujinx ne
//           lit pas les .nro), donc on fait parler la console : nextendo_trace() ecrit chaque
//           etape dans sdmc:/prelude_trace.txt et COMMIT a chaque ligne — sans ce commit les
//           ecritures restent en cache et un arret force les perd, ce qui est exactement ce qui
//           rendait le blocage invisible. La derniere ligne du fichier = la derniere etape
//           franchie. Suspect principal : le build 10 a introduit le PREMIER appel a
//           splInitialize() de l'appli (nextendo_detect_boot existait mais n'avait jamais ete
//           appele) ; si spl casse la session sm en mode applet, l'entree meurt et tout colle.
// build 12 : RELEASE de l'audit (= build 10 verifie sur console + trace conservee). Le build 11
//           a prouve sur la vraie console que les 4 correctifs passent de bout en bout :
//           detect_boot=SYSMMC, hosts supprimes, purges ok, "apply_nintendo: TERMINE". Constate
//           apres bascule : plus un seul patch IPS, plus de CA Nextendo, plus de rootCA.pem, plus
//           aucun log DNS-MITM ni .bak -> l'IP du VPS n'est plus nulle part sur la carte, sauf
//           dans ce .nro (inevitable : c'est lui qui ecrit les hosts).
//           La trace est GARDEE : le gel du build 10 n'a jamais ete explique (le suspect spl a ete
//           innocente par la trace elle-meme), donc si un joueur le revit, prelude_trace.txt est
//           notre seul temoin. Elle repart de zero a chaque lancement.
// build 13 : ECRAN DE REVUE AVANT APPLICATION (revertido en build 14). Mostraba los cambios
//           de config antes de aplicar. Causaba confusión y se revirtió al confirm estático.
// build 14 : REVERT del config review screen. Vuelve al ui_draw_confirm original.
//           Agregado GitHub Actions workflow para builds automáticos.
// v2.0.1 : Major bump + fix build 20. Hosts actualizados con redirecciones a VPS para penne_ids, dauth, srv.nintendo.net.
// Anterior v2.0.0 tenia NEXTENDO_BUILD 0, lo que provocaba falso "Mise à jour OBLIGATOIRE" (server v12 > build 0).
// v2.0.3 : Fix phantom update popup. Remove *.nintendo.net wildcard to let d4c resolve to real Nintendo.
//          En 22.5.0 el SSL del VPS no es confiable (disable_ca_verification no cubre 22.5.0),
//          nim no recibe meta de actualizacion -> popup fantasma. Al resolver d4c al Nintendo real,
//          22.5.0 se ve como "ultima version" -> sin popup.
// build 24 : Fix browser conntest (remove *.nintendowifi.net). Better BCAT error diagnostics with
//            specific network error codes (connect/timeout/HTTP). Added PIA + BCAT connectivity
//            tests to network diagnostics.
// build 25 : Auto-create BCAT save data if missing (fix Splatoon 2 schedule without launching S2).
//            Update confirmation screen (Y -> ask -> A = install, B = cancel) instead of immediate download.
// build 28 : v2.0.9. Fix input freeze (consoleUpdate(NULL) in main loop).
// build 29 : v2.1.0. Auto-update via GitHub API (HTTPS) directly, no VPS dependency.
//            Uses Switch native SSL service for HTTPS. Adds net_https_get/_to_file.
// build 30 : v2.1.1. Startup update check via HTTP (no SSL) to fix 'Función no disponible'
//            on login after exiting Prelude (sslInit/sslExit side effect on system SSL service).
// build 31 : v2.1.2. Fix browser "Función no disponible". Remove api.hac.lp1.ctest.srv.nintendo.net
//            and wildcards *.srv.nintendo.net / *srv.nintendo.net from DNS-MITM hosts so the
//            browser conntest (introduced FW 18.0+) resolves to real Nintendo instead of VPS.
// build 32 : v2.1.3. Fix Splatoon 2 schedule installer. Replace BCAT SaveData mount with
//            Atmosphere LayeredFS (sdmc:/atmosphere/contents/<title_id>/romfs/DebugUnderPilot/bcat/)
//            for both USA (01003BC0000A0000) and EUR (0100F8F0000A2000) versions.
// build 33 : v2.1.4. Set real Nextendo server IP (51.178.29.194) in config replacing placeholder.
// build 34 : v2.1.5. Splatoon 2 schedule: embed .byaml/.bfres data in NRO romfs instead of
//            downloading from server (which returned "no available server"). The installer now
//            copies files from romfs:/bcatdata/ to LayeredFS paths on SD — no network needed.
// build 35 : v3.0.0. New BCAT schedules (Aug 3 EU, Jun 29 US). Wildcard g2*.s.n.srv.nintendo.net
//            covers ALL game NEX secure servers (S2, MK8, SSBU, ACNH, Strikers). Fix linking
//            (extras.self nxAccountBlob, api/token revert). NDAS aauth host + handler.
// build 36 : v3.0.2. Remove *.op2.nintendo.net wildcard (caused 2219-4001 on ACNH). Add
//            conntest.nintendowifi.net + ctest.cdn.nintendo.net redirects (fix browser
//            "This feature is not available"). BCAT from bcat-seed.zip.
// build 40 : v3.0.6. MK8 secure-server (g2b309e01-lp1) now routes to the production IP
//           (164.132.111.120) instead of the current server, where Mario Kart has its
//           players. Kept AFTER the g2* wildcard: "last matching line wins" in Atmosphere.
// build 41 : v3.0.7. Fix auto-updater: (a) semver comparison was broken — the tag patch
//           (e.g. 6) was compared against NEXTENDO_BUILD (40), so 6 > 40 was always false
//           and updates were never detected. Now compares full maj.min.patch against
//           NEXTENDO_VERSION_*; (b) GitHub API now points to the upstream repo
//           (NextendoNetwork/Prelude-Nro) instead of the fork, which lagged a release
//           behind. UI shows the full version (v3.0.7) in banner/confirm/status.
// build 42 : v3.0.8. Fix BCAT download (always showed "write error"). Root cause: raw
//           sslConnectionSetSocketDescriptor (IPC) was called directly — libnx docs say
//           "Do not use directly, use socketSslConnectionSetSocketDescriptor instead".
//           The raw call failed with 0xe87b (module 123 / description 116) before any
//           HTTP header was received, returning status=0 which was treated as success,
//           triggering a fake "write error" from the empty zip. Fixed in both
//           net_https_get and net_https_get_to_file.
// build 43 : v3.0.9. BCAT: download EUR zip once (0100F8F0000A2000) and install it to
//           all regions (USA + EUR). Avoids a second HTTPS round-trip and ensures both
//           regions always get the same schedule data regardless of per-region API state.
// build 44 : v3.1.0. Fix auto-updater never detecting updates. Root cause: the GitHub
//           API returns pretty-printed JSON ("tag_name": "vX.Y.Z" with a space after
//           the colon), but parse_github_json searched for "tag_name":"" (no space) —
//           strstr never matched, parse returned false, update was always skipped.
//           Replaced hardcoded key+quote strings with json_str_value() which tolerates
//           optional whitespace around ':'. Same fix for browser_download_url.
// build 45 : v3.2.0. MK8D country flag installer. New button alongside BCAT in the
//           picker (FOCUS_FLAG). Opens a scrollable menu of 110 countries sourced
//           from alyeri/nextendo-mk8d-country-flags. Downloads the IPS ExeFS patch
//           (title 0100152000022000, build ID FE941ED5BA14BE5D505698DA1BBF4FE7) from
//           GitHub raw and installs it at:
//               sdmc:/atmosphere/exefs_patches/Nextendo Country XX/
//           Removes any previously installed flag before writing the new one. Shows
//           the installed country code in the flag bar while in the picker.
// build 46 : v3.2.2. SSBU online-deluxe quickplay mod bundled in NRO romfs.
//           Automatically installed when switching to Nextendo mode and removed when
//           switching back to Nintendo mode. Mod files bundled at romfs:/ssbu_quickplay/
//           and mirrored to sdmc:/atmosphere/contents/ via copyTreeRomfs / removeTreeRomfs.
//           Title 01006A800016E000 (SSBU) + sysmodule 00FF0000A11CE0FF (online-deluxe v1.3.0).
//           Full dependency stack: arcropolis, nro-hook, smashline, imgui-smash, ssbu-pia-manager.
// build 47 : v3.2.3. Luigi's Mansion 3 online support. Add explicit DNS-MITM host for
//           LM3 NEX game server (g20de2100-lp1.s.n.srv.nintendo.net -> VPS).
//           Already caught by the g2* wildcard but added explicitly for reliability,
//           consistent with SSBU / ACNH / Strikers entries.
// build 48 : v3.2.4. Fix auto-updater download "fallo de red". Root cause: GitHub
//           releases URLs redirect (HTTP 302) to a CDN (objects.githubusercontent.com)
//           with a presigned URL. net_https_get_to_file did not follow redirects,
//           causing NUP_NET_FAIL on every download attempt. Fixed by following one
//           redirect: on 3xx response, Location header is extracted, file is truncated,
//           and a second HTTPS connection is made to the CDN host.
// build 49 : v3.2.5. SSBU mod now optional. Removed auto-install on Nextendo mode switch.
//           New SSBU mod menu (L button on picker): shows install status, A to install
//           or uninstall SSBU Online Deluxe quickplay mod. Removal on Nintendo mode
//           switch is kept for cleanup. Detection via sentinel file on SD card.
// build 50 : v3.2.6. Toggle for the SSBU mod's OWN overclock (X button on the SSBU
//           screen). The mod bundles its own overclocker — the libnx_over.nro skyline
//           plugin plus sysmodule 00FF0000A11CE0FF, loaded at boot by flags/boot2.flag.
//           When the player already runs Horizon OC / sys-clk, both drive the same PCV
//           clock rails and the console FREEZES on launching Smash or seconds later
//           (tell-tale sign: renaming the Horizon OC folder makes the mod work).
//           Disabling writes sdmc:/ultimate/ssbu_online_deluxe/config.toml with
//           overclocker = false, then deletes the plugin and the sysmodule — procedure
//           confirmed by saad-script (mod author). Reversible offline: re-enabling
//           copies both back from the NRO romfs, no download needed.
//           State is read from flags/boot2.flag (what actually makes Atmosphere load
//           the sysmodule), never from config.toml, which the player may have hand-edited.
//           The option is hidden and inert unless the mod is installed: without it,
//           restoring 00FF0000A11CE0FF would leave an ORPHAN overclocker active at boot.
// build 51 : v3.2.7. Honest wording for NINTENDO mode (reported by deejay87). The mode
//           sets blank_prodinfo_emummc=1, so on an emuMMC console the device identity is
//           blanked and online services cannot authenticate — no eShop, no online play.
//           The UI nevertheless promised "normal console operation" / "back to official
//           Nintendo servers", which never happened for those users. The picker line is
//           now factual for both console types, and the confirm screen picks its wording
//           from warnNoEmummc: sysNAND keeps the old text (blank_prodinfo has no effect
//           there, so the return really is complete, and the existing risk warning still
//           shows below it), emuMMC gets a new string stating online will not work.
//           Also fixes a misleading comment in nextendo_apply.c: Atmosphere's default
//           telemetry entries are COMPILED INTO the DNS.mitm sysmodule, not read from a
//           default.txt it creates. add_defaults_to_dns_hosts=1 merges them, so blocking
//           holds from boot even with an empty /atmosphere/hosts/. Documents why we must
//           NOT write our own default.txt: it belongs to the user, would outlive Prelude,
//           and our copy of the list would drift behind Atmosphere's.
// build 52 : v3.3.0. Three changes: the UI adopts the console's own look, the .nro
//           loses a third of its size, and Splatoon 3 becomes reachable.
//
//           (0) UI RETHEMED. The palette now follows the system (HOME menu / Settings)
//           instead of a bespoke identity, and it tracks the console's light/dark
//           setting — so the colours became theme_* accessors rather than compile-time
//           constants, since a constant cannot change at runtime. The two brand colours
//           (Nextendo blue, Nintendo red) are deliberately left alone: they are the
//           project's identity and they hold on either background. Rationale: Prelude
//           IS a settings app, so it should use the conventions its users already know.
//
//           (a) AUDIO REWRITTEN, SDL2 DROPPED. Background music moves from SDL2_mixer
//           to mpg123 + audout directly. The old link line, `pkg-config --static
//           --libs SDL2_mixer`, pulled in libSDL2 (4.4 MB) and with it Mesa —
//           libEGL (5.1 MB) plus libglapi — because SDL2 declares a video subsystem,
//           plus libmodplug (0.9 MB) for a tracker format we never use. Prelude draws
//           straight to the framebuffer and has never issued an OpenGL call: all of
//           that existed only to decode one MP3, and mpg123 (0.24 MB) is the only
//           part that was doing the work.
//           The easter egg went too (audio.mp3 + 671 JPEG frames, 2.6 MB).
//           Measured: .nro 25.97 MB -> 17.01 MB, code section 8.47 MB -> 2.13 MB
//           (-75 %), romfs 17.50 MB -> 14.88 MB.
//           This matters beyond disk: hbloader loads the whole .nro into RAM, and in
//           applet mode that headroom is what the updater needs to buffer a download.
//
//           (b) SPLATOON 3 / NPLN HOSTS. Splatoon 3 does NOT use NEX — it speaks
//           NPLN (gRPC over HTTP/2), so it has no g2*.s.n secure server and none of the
//           existing per-game entries reach it. Hosts taken from the traefik HostSNI
//           rules on the live npln container, not guessed:
//             t-dce9377b-lp1.lp1.t.npln.srv.nintendo.net
//             t-adf89f68-lp1.lp1.t.npln.srv.nintendo.net
//             gw.hac.lp1.vermillion.srv.nintendo.net
//             val.hac.lp1.penne.srv.nintendo.net
//             fro-3.hac.lp1.penne.srv.nintendo.net
//             dragons.hac.lp1.dragons.nintendo.net
//           The first five end in srv.nintendo.net and were already caught by the
//           *srv wildcard; they are listed explicitly for the same reason as the NEX
//           games (mid-label * is ignored on some Atmosphere builds).
//           dragons is the one that was genuinely UNREACHABLE: it ends in .nintendo.net,
//           not srv.nintendo.net, and there is no generic *.nintendo.net (that was
//           removed in v3.0.2 because it was too broad). Without this line it resolved
//           to the real Nintendo.
//           Note: an existing entry has val.hac.penne.srv.nintendo.net without the lp1
//           label; the live route is val.hac.lp1.penne.srv.nintendo.net. Both are kept.
// build 53 : v3.3.1. Splatoon 3 : le PATCH CERTIFICAT, sans lequel rien ne marche.
//           v3.3.0 annoncait le support Splatoon 3 en n'ajoutant que des hosts. Ca ne
//           pouvait pas suffire : le client NPLN de Splatoon 3 lie STATIQUEMENT son
//           propre BoringSSL et pilote TLS lui-meme sur des sockets Bsd bruts. Il ne
//           passe JAMAIS par le service ssl: de la console — ni sur hardware reel, ni
//           en emulation. Donc disable_ca_verification, qui patche le module SSL du
//           systeme, n'est jamais consulte par ce jeu : quoi que dise le DNS, le jeu
//           valide notre certificat contre son propre jeu epingle, dans sa propre pile
//           TLS, et coupe des le ClientHello.
//           Le contournement doit donc atterrir dans le NSO du jeu. On embarque les
//           deux patches IPS32 de Kazu dans exefs_patches (deja deployes par
//           copyTreeRomfs, et retires au retour en mode Nintendo comme le reste) :
//             s3certbypass  19 o, 1 record  -> MOV W10,#0 a 0x157B20 (check cert)
//             s3peername    29 o, 2 records -> NOP a 0x14E1B0 + MOV W20,WZR a 0x14DD80
//           Build ids : 6830B3A1... = 11.2.0, 726D2B88... = 11.0.0. Le certbypass est
//           BYTE-IDENTIQUE pour les deux (le code n'a pas bouge entre les versions,
//           seul le nom de fichier change) ; s3peername n'existe que pour 11.2.0.
//
//           /!\ VERSION-SPECIFIQUE, et le mode d'echec est silencieux. Atmosphere
//           matche sur le build id du NOM DE FICHIER : une MAJ du jeu produit un
//           nouveau build id et le patch est simplement IGNORE — pas d'erreur, pas de
//           log, juste l'ecran de chargement. C'est deja arrive : un patch construit
//           sur un dump 11.0.0 dormait sur une console 11.2.0 pendant qu'on accusait
//           le handshake TLS. Les offsets INTERNES sont un risque separe du nom : ils
//           n'ont pas bouge entre 11.0.0 et 11.2.0, rien ne garantit que ca tienne a
//           la prochaine MAJ. Attendu en cas de MAJ Splatoon 3 : les patches cessent
//           de s'appliquer, tout le monde reste sur l'ecran de chargement, et il faut
//           un nouveau dump pour les reconstruire.
//
//           Cote hosts on ajoute quand meme gamesync.npln.nintendo.net (+ le wildcard
//           *.npln.nintendo.net) : il finit par npln.nintendo.net et non par
//           srv.nintendo.net, donc le wildcard *srv ne le prend pas. MAIS ce n'etait
//           PAS le bloqueur : d'apres Kazu le client ne resout pas ce nom, le serveur
//           lui donne une adresse et un port explicites dans le document de session et
//           le token ; le hostname ne sert qu'a l'authority et au SNI. On le garde par
//           completude, pas comme correctif.
//           (ancienne entree de ce build, conservee pour l'historique :)
//           HOTFIX Splatoon 3 : le lobby ne se connectait jamais.
//           v3.3.0 a ajoute le tenant (t-*.lp1.t.npln.srv...) mais pas le SESSION
//           HOST, qui est un service separe portant le lobby lui-meme : stream
//           bidirectionnel KeepUserSession sur TCP/7575, :authority =
//           gamesync.npln.nintendo.net.
//           Pourquoi il avait ete manque : les autres hotes NPLN ont ete tires des
//           regles HostSNI de traefik, or gamesync n'y figure pas — c'est un port TCP
//           direct de l'hote (nplns3-gamesync ecoute sur 7575), pas un service derriere
//           le proxy. Et comme dragons, il finit par npln.nintendo.net et non par
//           srv.nintendo.net, donc le wildcard *srv ne le rattrapait pas non plus.
//           Resultat cote joueur : le tenant repondait (comptes, amis, horaires) mais
//           aucune partie ne demarrait. On ajoute *.npln.nintendo.net + l'hote explicite.
// build 54 : v3.3.2. TELEMETRIE : le mode NEXTENDO ne fusionnait pas les entrees
//           par defaut d'Atmosphere. On appelait iniSetDnsMitm(true, FALSE) — le
//           second argument est add_defaults_to_dns_hosts — donc la table native
//           d'Atmosphere etait desactivee et le seul blocage etait nos deux lignes
//           receive-%.dg/er. Sur le mode ou les gens jouent, precisement.
//           Le mode NINTENDO fait (true, true) depuis le build 10, avec un commentaire
//           expliquant pourquoi : ne pas maintenir notre propre liste, laisser celle
//           d'Atmosphere qui est suivie en amont et ne derivera pas. Ce raisonnement
//           n'avait jamais ete reporte dans apply_nextendo. Corrige.
//           Signale publiquement par TherealJaw, qui avait raison. Verifie avant de
//           corriger : les defauts d'Atmosphere ne couvrent que les serveurs de
//           telemetrie (receive-%), jamais accounts.nintendo.com ni les hotes de jeu,
//           donc aucun risque qu'ils ecrasent nos redirections. Le seul recouvrement
//           est receive-%, ou les deux valeurs bloquent (0.0.0.0 / 127.0.0.1).
// build 55 : v3.3.3. Deux corrections, dont une signalee par presque tous les utilisateurs.
//           AUTO-UPDATER : il ecrivait TOUJOURS dans sdmc:/switch/nextendo.nro, un chemin
//           fixe, sans regarder d'ou le .nro tournait. Pour quiconque gardait Prelude
//           ailleurs (autre nom, sous-dossier, racine de la carte), la mise a jour
//           deposait donc un fichier EN PLUS : celui qu'il relancait restait l'ancien.
//           Les DEUX rapports recus n'en faisaient qu'un seul — « l'updater ne marche
//           pas » (on relance l'ancienne version) et « il devrait remplacer Prelude »
//           (les deux versions sont conservees). Le seul utilisateur pour qui ca
//           fonctionnait etait celui dont le chemin coincidait avec la constante : ce
//           n'est pas son dossier qui a aide, c'est la coincidence.
//           hbmenu passe le chemin REEL du .nro dans argv[0], et main() le jetait
//           (parametre inutilise). On s'en sert : l'updater remplace desormais le fichier
//           LANCE, et le .new est ecrit a cote de la cible pour que rename() ne traverse
//           rien. Repli sur l'ancien chemin si argv est inexploitable — mieux vaut le
//           comportement historique qu'une ecriture a un endroit invente.
//           Le duplicat orphelin laisse a l'ancien emplacement par les versions
//           precedentes est supprime apres une mise a jour reussie, et lui seul.
//           ATTENTION : ce correctif ne profite a personne avant d'etre installe A LA
//           MAIN. Une version cassee mettra encore a jour a cote ; c'est a partir de la
//           3.3.3 que l'updater se remplace lui-meme.
//
//           SAUVEGARDE DES HOSTS UTILISATEUR : le mode NINTENDO supprime atmosphere/
//           hosts/{sysmmc,emummc}.txt. Qui arrivait avec ses PROPRES redirections (un
//           autre serveur communautaire, ses propres blocages) les perdait des le premier
//           passage par Prelude, sans le moindre avertissement. Au tout premier
//           lancement, UNE SEULE FOIS, deux questions : copier les hosts existants vers
//           switch/prelude_hosts_backup, puis les remettre ou non au retour en mode
//           NINTENDO. La seconde question ne se pose que si la copie a donne quelque
//           chose. On ne sauvegarde JAMAIS un fichier que NOUS avons ecrit : d'abord
//           parce que garder notre IP sur la carte est exactement ce que
//           nextendo_purge_leaks() existe pour empecher, ensuite parce que le remettre en
//           mode NINTENDO ferait parler toute la console a nos serveurs alors qu'elle est
//           censee etre revenue chez Nintendo. La reconnaissance se fait sur l'EN-TETE que
//           nous ecrivons, pas seulement sur les deux IP courantes : un hosts pose par une
//           version tres ancienne, ou par une installation manuelle vers une autre adresse,
//           passerait un filtre base sur les IP et serait restaure vers un serveur mort.
// build 56 : v3.3.4. Correctif du build 55, publie tout de suite apres lui parce qu'il
//           ne peut pas se reparer tout seul chez ceux qui ont deja lance le 55.
//           Le garde-fou qui empeche de sauvegarder un hosts ecrit par NOUS ne
//           s'appliquait qu'a la CREATION de la copie, et il ne reconnaissait nos
//           fichiers qu'a leurs deux IP courantes. Un hosts pose par un Prelude tres
//           ancien (autre IP, en-tete non testee a l'epoque) passait donc le filtre : il
//           etait copie, puis remis a CHAQUE passage en mode NINTENDO, et la console
//           parlait a un serveur mort. Sans issue pour l'utilisateur, en plus : la
//           question ne se pose qu'une fois et sa reponse est deja enregistree.
//           Deux verrous ajoutes. On reconnait nos fichiers a l'EN-TETE ecrite par
//           nextendo_hosts_build, quelle que soit l'IP qu'ils portaient. Et on revalide
//           la sauvegarde AVANT de la remettre, pas seulement avant de la creer : c'est
//           ce qui repare les installations ou la mauvaise copie existe deja, sans rien
//           demander a personne. Une copie invalide n'est simplement jamais restauree.
//           Rien d'autre ne change par rapport au 55.
// build 57 : v3.3.5. L'updater du 55/56 pouvait echouer sur « impossible d'ecrire sur
//           la SD » (rapporte par Andrei, depuis la 3.3.3). Le 55 a fait passer la cible
//           de « un chemin fixe » a « le fichier qu'on execute ». Le code disait que
//           l'ecrasement etait sans risque parce qu'on tourne depuis la RAM : c'etait
//           gratuit tant que le fichier remplace n'etait PAS celui qu'on executait, et ca
//           a cesse de l'etre precisement avec ce changement. Selon le chargeur, le .nro
//           en cours peut rester ouvert — remove() echoue, rename() ne peut pas ecraser
//           sur FAT32, l'ouverture en ecriture est refusee, et l'utilisateur se retrouve
//           avec une erreur alors que la mise a jour est deja telechargee.
//           Trois tentatives au lieu d'une : ecrasement EN PLACE sans supprimer d'abord
//           (passe si le fichier resiste a la suppression mais pas a l'ecriture), puis
//           remove+rename, puis en dernier recours l'ancien emplacement fixe. Ce dernier
//           recours recree le doublon que le 55 corrigeait : c'est assume, une mise a
//           jour installee ailleurs vaut mieux qu'une mise a jour perdue.
//           L'updater ecrit desormais dans prelude_trace.txt ce qu'il a fait (60 a 64) :
//           il n'avait AUCUNE trace, et ce diagnostic-ci a du etre deduit au lieu d'etre
//           lu. La prochaine fois, le fichier de l'utilisateur repondra tout seul.
// build 58 : v3.3.6. Splatoon 3 sur les versions RECENTES du jeu. Les correctifs de
//           certificat sont indexes par identifiant de build : quand Nintendo publie une
//           mise a jour, l'identifiant change, Atmosphere ne trouve plus rien et n'applique
//           RIEN — en silence. Le joueur voit 2122-2403 au demarrage en ligne sans le
//           moindre indice. Signale par cakita (Prelude 3.3.2, jeu 11.2.0) et par plusieurs
//           testeurs le 16 aout ; la 11.3.0 est sortie le 20.
//           Ce build ajoute l'identifiant 28C4287A (11.2.0 / 11.3.0), fourni par Kazu, pour
//           s3certbypass ET s3peername. Chaque correctif est livre sous ses DEUX formes de
//           nom, la courte (40 hex) et la completee a 64 : les deux conventions existent
//           selon les versions d'Atmosphere, et un nom qui ne correspond pas ne produit
//           aucun message.
//           Verifie avant integration : les trois identifiants portent la MEME instruction
//           au MEME offset (0x00157B20 <- 2a008052, MOV W10, #1), ce qui confirme que la
//           fonction visee ne bouge pas d'une version a l'autre. s3peername (0x0014E1B0 NOP
//           + 0x0014DD80 MOV W20, WZR) reste absent pour 726D2B88, sans consequence : le
//           certificat que nous servons porte le bon CN et les bons SAN, la verification du
//           nom passe donc d'elle-meme.
//           Ce que ce build NE resout PAS : la prochaine mise a jour du jeu cassera encore
//           tout. Le correctif de fond est un parcheur par MOTIF plutot que par identifiant
//           de build — kinnay publie deja un script qui balaie les motifs d'instructions
//           dans un NSO et genere l'IPS (NPLN-Protocols/generate_patch.py), ce qui ramene
//           chaque nouvelle version a quelques minutes en attendant mieux.
#define NEXTENDO_BUILD 58

// Version SEMVER de CE build. Doit rester alignee avec APP_VERSION (Makefile).
// Le compare a l'updater se fait en semver complet (maj.min.patch), pas avec
// NEXTENDO_BUILD : les tags GitHub sont des semver (v3.2.5), pas des compteurs.
#define NEXTENDO_VERSION_MAJOR 3
#define NEXTENDO_VERSION_MINOR 3
#define NEXTENDO_VERSION_PATCH 6

typedef struct {
    bool available;   // une version semver > NEXTENDO_VERSION_* est dispo
    int  maj, min, patch;  // version serveur (du tag semver)
    long size;        // taille attendue du .nro (verif du telechargement)
} NextendoUpdate;

typedef enum {
    NUP_OK = 0,
    NUP_NET_FAIL,     // telechargement impossible
    NUP_SIZE_FAIL,    // taille recue != taille annoncee (download corrompu)
    NUP_WRITE_FAIL    // ecriture SD impossible
} nextendo_update_result;

// Interroge le serveur (rapide, timeout court). socketInitializeDefault géré en interne.
// Chemin du .nro en cours d'execution (argv[0] de hbmenu). A appeler AVANT toute mise
// a jour : c'est ce fichier-la qui sera remplace. Sans cet appel, l'updater retombe sur
// l'emplacement historique sdmc:/switch/nextendo.nro.
void nextendo_update_set_self_path(const char *argv0);

NextendoUpdate nextendo_update_check(void);

// Telecharge le nouveau .nro et remplace /switch/nextendo.nro (via un .new + rename).
nextendo_update_result nextendo_update_apply(long expectedSize);

#endif // NEXTENDO_UPDATE_H
