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

#include <switch.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <dirent.h>
#include <errno.h>

#include "nextendo_flag.h"

// 110 countries from MK8D 3.0.5 internal table, sorted by code.
//
// CE NOMBRE EST CELUI DU JEU, pas le notre. Le patch n encode pas un index : il ecrit les
// DEUX LETTRES du code en ASCII, donc flag_build_ips() sait fabriquer N IMPORTE QUEL code
// a deux lettres. La tentation est alors d en ajouter — CN / HK / TW ont ete ajoutes ici
// le 2026-08-23 pour l issue #17, puis RETIRES : alyeri, qui a releve la table d origine
// dans le jeu, confirme que MK8D n a pas ces drapeaux. Le patch fait bien dire "CN" a la
// console, mais le jeu n a aucune image a afficher en face.
//
// Donc : pouvoir fabriquer le patch ne veut pas dire que le pays existe. N ajouter une
// entree ici QU APRES l avoir vue s afficher en jeu.
const FlagEntry g_flags[FLAG_COUNT] = {
    {"AE","United Arab Emirates"}, {"AL","Albania"},   {"AO","Angola"},
    {"AR","Argentina"},            {"AT","Austria"},    {"AU","Australia"},
    {"AW","Aruba"},                {"AZ","Azerbaijan"},
    {"BA","Bosnia and Herzegovina"},{"BB","Barbados"},  {"BE","Belgium"},
    {"BG","Bulgaria"},             {"BN","Brunei"},     {"BO","Bolivia"},
    {"BR","Brazil"},               {"BS","Bahamas"},    {"BW","Botswana"},
    {"BY","Belarus"},              {"BZ","Belize"},
    {"CA","Canada"},               {"CH","Switzerland"},{"CL","Chile"},
    {"CO","Colombia"},             {"CY","Cyprus"},     {"CZ","Czechia"},
    {"DE","Germany"},              {"DK","Denmark"},    {"DO","Dominican Republic"},
    {"EC","Ecuador"},              {"EE","Estonia"},    {"EG","Egypt"},
    {"ES","Spain"},
    {"FI","Finland"},              {"FR","France"},
    {"GB","United Kingdom"},       {"GH","Ghana"},      {"GR","Greece"},
    {"GT","Guatemala"},
    {"HN","Honduras"},             {"HR","Croatia"},    {"HU","Hungary"},
    {"ID","Indonesia"},            {"IE","Ireland"},    {"IL","Israel"},
    {"IN","India"},                {"IS","Iceland"},    {"IT","Italy"},
    {"JM","Jamaica"},              {"JO","Jordan"},     {"JP","Japan"},
    {"KR","South Korea"},          {"KW","Kuwait"},     {"KZ","Kazakhstan"},
    {"LB","Lebanon"},              {"LT","Lithuania"},  {"LU","Luxembourg"},
    {"LV","Latvia"},
    {"MA","Morocco"},              {"MD","Moldova"},    {"MG","Madagascar"},
    {"MK","North Macedonia"},      {"ML","Mali"},       {"MN","Mongolia"},
    {"MR","Mauritania"},           {"MT","Malta"},      {"MU","Mauritius"},
    {"MX","Mexico"},               {"MY","Malaysia"},   {"MZ","Mozambique"},
    {"NA","Namibia"},              {"NG","Nigeria"},    {"NI","Nicaragua"},
    {"NL","Netherlands"},          {"NO","Norway"},     {"NZ","New Zealand"},
    {"OM","Oman"},
    {"PA","Panama"},               {"PE","Peru"},       {"PG","Papua New Guinea"},
    {"PH","Philippines"},          {"PK","Pakistan"},   {"PL","Poland"},
    {"PT","Portugal"},             {"PY","Paraguay"},
    {"QA","Qatar"},
    {"RO","Romania"},              {"RS","Serbia"},     {"RU","Russia"},
    {"RW","Rwanda"},
    {"SC","Seychelles"},           {"SE","Sweden"},     {"SG","Singapore"},
    {"SI","Slovenia"},             {"SK","Slovakia"},   {"SR","Suriname"},
    {"SV","El Salvador"},          {"SZ","Eswatini"},
    {"TD","Chad"},                 {"TH","Thailand"},   {"TN","Tunisia"},
    {"TR","Turkiye"},              {"TT","Trinidad and Tobago"}, {"TZ","Tanzania"},
    {"UA","Ukraine"},              {"UG","Uganda"},     {"US","United States"},
    {"VE","Venezuela"},            {"VN","Vietnam"},
    {"ZM","Zambia"},               {"ZW","Zimbabwe"},
};

#define EXEFS_PATCHES_DIR "sdmc:/atmosphere/exefs_patches"
#define FLAG_FOLDER_PREFIX "Nextendo Country "
#define BUILD_ID "FE941ED5BA14BE5D505698DA1BBF4FE7"
// Plus de telechargement : le patch est fabrique localement (voir flag_build_ips).
// L amont reste alyeri/nextendo-mk8d-country-flags, dont ce code reproduit la sortie
// a l octet pres pour les 110 pays qu il publie.

int flag_find_index(const char *code) {
    for (int i = 0; i < FLAG_COUNT; i++)
        if (strncmp(g_flags[i].code, code, 2) == 0) return i;
    return -1;
}

void flag_detect_current(char out_code[3]) {
    out_code[0] = '\0';
    out_code[1] = '\0';
    out_code[2] = '\0';

    DIR *d = opendir(EXEFS_PATCHES_DIR);
    if (!d) return;

    struct dirent *e;
    size_t pfxLen = strlen(FLAG_FOLDER_PREFIX);
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        size_t nlen = strlen(e->d_name);
        if (nlen == pfxLen + 2
            && strncmp(e->d_name, FLAG_FOLDER_PREFIX, pfxLen) == 0) {
            out_code[0] = e->d_name[pfxLen];
            out_code[1] = e->d_name[pfxLen + 1];
            out_code[2] = '\0';
            break;
        }
    }
    closedir(d);
}

void flag_remove(void) {
    DIR *d = opendir(EXEFS_PATCHES_DIR);
    if (!d) return;

    struct dirent *e;
    size_t pfxLen = strlen(FLAG_FOLDER_PREFIX);
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        size_t nlen = strlen(e->d_name);
        if (nlen == pfxLen + 2
            && strncmp(e->d_name, FLAG_FOLDER_PREFIX, pfxLen) == 0) {
            // Remove the IPS file inside, then the directory.
            char ipsPath[FS_MAX_PATH];
            snprintf(ipsPath, sizeof(ipsPath), "%s/%s/" BUILD_ID ".ips",
                     EXEFS_PATCHES_DIR, e->d_name);
            remove(ipsPath);
            char dirPath[FS_MAX_PATH];
            snprintf(dirPath, sizeof(dirPath), "%s/%s", EXEFS_PATCHES_DIR, e->d_name);
            rmdir(dirPath);
        }
    }
    closedir(d);
}

// --- Fabrication LOCALE du patch, sans reseau ---------------------------------------
//
// Les 110 patches du depot alyeri sont le MEME fichier de 103 octets ; seules CINQ
// positions changent, et ce qu'on y ecrit n'est pas un index de pays mais les deux
// LETTRES du code en ASCII. Le jeu recoit "JP" parce que le patch fait MOVZ W8,#0x4A
// ('J') puis MOVZ W8,#0x50 ('P'), a deux endroits, plus les deux lettres empaquetees
// en un seul immediat 16 bits au troisieme.
//
// Verifie, pas suppose : en repartant du seul patch JP et en substituant ces cinq
// positions, on regenere les 110 patches du depot A L'OCTET PRES (110/110, 2026-08-23).
//
// D'ou ce choix : telecharger 103 octets en HTTPS depuis GitHub pour y placer deux
// caracteres ASCII etait un aller-retour reseau — donc un mode d'echec, "fallo de red"
// — pour une donnee que l'application peut ecrire elle-meme. La generation locale
// supprime cette panne, marche hors ligne, et rend n'importe quel code a deux lettres
// installable : c'est ce qui permet CN / HK / TW (issue #17) sans rien publier en amont.
//
// CE QUE CA NE GARANTIT PAS : que MK8D possede la TEXTURE du drapeau demande. Le patch
// dit au jeu "je suis CN" ; si sa table interne n'a pas ce pays, l'affichage sera vide
// ou incorrect. Les 110 d'origine viennent de cette table, les trois nouveaux non.
//
// Si MK8D est mis a jour, le build id change et Atmosphere ignore le patch — mais
// BUILD_ID est deja code en dur ici, donc ce cas imposait deja une MAJ de Prelude,
// telechargement ou pas. On ne perd rien.
#define FLAG_IPS_LEN 103
// Decalages des cinq octets a substituer (immediats des MOVZ), releves sur les patches
// reels et non calcules : records 1 et 2 portent chacun les deux lettres, le record 3
// les porte empaquetees.
#define FLAG_OFF_A1 10
#define FLAG_OFF_Z1 18
#define FLAG_OFF_A2 43
#define FLAG_OFF_Z2 51
#define FLAG_OFF_PK 76

static const unsigned char FLAG_IPS_TEMPLATE[FLAG_IPS_LEN] = {
    0x50, 0x41, 0x54, 0x43, 0x48, 0x86, 0xD5, 0xE4, 0x00, 0x1C, 0x48, 0x09,
    0x80, 0x52, 0x68, 0x02, 0x02, 0x39, 0x08, 0x0A, 0x80, 0x52, 0x68, 0x06,
    0x02, 0x39, 0xE8, 0x03, 0x00, 0x32, 0x68, 0x0E, 0x02, 0x39, 0x0E, 0x00,
    0x00, 0x14, 0x86, 0xD6, 0x74, 0x00, 0x1C, 0x48, 0x09, 0x80, 0x52, 0x68,
    0x02, 0x02, 0x39, 0x08, 0x0A, 0x80, 0x52, 0x68, 0x06, 0x02, 0x39, 0xE8,
    0x03, 0x00, 0x32, 0x68, 0x0E, 0x02, 0x39, 0x0E, 0x00, 0x00, 0x14, 0x87,
    0x51, 0x78, 0x00, 0x18, 0x48, 0x09, 0x8A, 0x52, 0xE8, 0x0B, 0x00, 0xB9,
    0xE0, 0x23, 0x00, 0x91, 0x7B, 0xE0, 0xFF, 0x97, 0x60, 0xE2, 0x00, 0x79,
    0x0A, 0x00, 0x00, 0x14, 0x45, 0x4F, 0x46,
};

// putMovzImm reecrit l'immediat d'un MOVZ W8 32 bits en place. Encodage AArch64 :
// 0x52800000 | (imm16 << 5) | Rd, ecrit en petit-boutiste. Le registre et le reste de
// l'instruction viennent de la plantilla, on ne touche QUE l'immediat.
static void putMovzImm(unsigned char *p, unsigned int imm16) {
    unsigned int w = 0x52800000u | ((imm16 & 0xFFFFu) << 5) | 8u;
    p[0] = (unsigned char)(w);
    p[1] = (unsigned char)(w >> 8);
    p[2] = (unsigned char)(w >> 16);
    p[3] = (unsigned char)(w >> 24);
}

// flag_build_ips ecrit dans out le patch du pays demande. code doit etre deux lettres
// majuscules ASCII ; tout le reste est refuse plutot que de produire un patch qui
// ferait ecrire n'importe quoi dans l'ExeFS du jeu.
static bool flag_build_ips(const char *code, unsigned char out[FLAG_IPS_LEN]) {
    if (!code || !code[0] || !code[1] || code[2]) return false;
    unsigned int a = (unsigned char)code[0];
    unsigned int z = (unsigned char)code[1];
    if (a < 'A' || a > 'Z' || z < 'A' || z > 'Z') return false;

    memcpy(out, FLAG_IPS_TEMPLATE, FLAG_IPS_LEN);
    putMovzImm(out + FLAG_OFF_A1, a);
    putMovzImm(out + FLAG_OFF_Z1, z);
    putMovzImm(out + FLAG_OFF_A2, a);
    putMovzImm(out + FLAG_OFF_Z2, z);
    putMovzImm(out + FLAG_OFF_PK, a | (z << 8));
    return true;
}

int flag_install(const char *code) {
    unsigned char ips[FLAG_IPS_LEN];
    if (!flag_build_ips(code, ips)) return -1;
    const unsigned char *data = ips;
    const size_t len = FLAG_IPS_LEN;

    // Remove any previously installed flag.
    flag_remove();

    // Create destination directory.
    char flagDir[FS_MAX_PATH];
    snprintf(flagDir, sizeof(flagDir), "%s/" FLAG_FOLDER_PREFIX "%s",
             EXEFS_PATCHES_DIR, code);

    mkdir(EXEFS_PATCHES_DIR, 0777);
    if (mkdir(flagDir, 0777) != 0 && errno != EEXIST) {
        return -2;
    }

    // Write the IPS patch.
    char ipsPath[FS_MAX_PATH];
    snprintf(ipsPath, sizeof(ipsPath), "%s/" BUILD_ID ".ips", flagDir);

    FILE *f = fopen(ipsPath, "wb");
    if (!f) { return -2; }

    bool ok = (fwrite(data, 1, len, f) == len);
    fclose(f);

    if (!ok) { remove(ipsPath); return -2; }

    fsdevCommitDevice("sdmc");
    return 0;
}
