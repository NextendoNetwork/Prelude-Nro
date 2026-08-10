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
#include "nextendo_net.h"

// 110 countries from MK8D 3.0.5 internal table, sorted by code.
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
#define GITHUB_HOST "raw.githubusercontent.com"
// Path template: fill %s with the 2-letter code (twice). Spaces encoded as %%20 -> %20.
#define GITHUB_PATH_FMT \
    "/alyeri/nextendo-mk8d-country-flags/main/Consoles/Atmosphere/%s" \
    "/atmosphere/exefs_patches/Nextendo%%20Country%%20%s/" BUILD_ID ".ips"

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

int flag_install(const char *code) {
    // Build the GitHub raw URL path.
    char path[256];
    snprintf(path, sizeof(path), GITHUB_PATH_FMT, code, code);

    // Download the IPS patch into memory.
    size_t len = 0;
    int status = 0;
    unsigned char *data = net_https_get(GITHUB_HOST, path, &len, &status);
    if (!data || status != 200 || len < 8) {
        if (data) free(data);
        return -1;
    }

    // Remove any previously installed flag.
    flag_remove();

    // Create destination directory.
    char flagDir[FS_MAX_PATH];
    snprintf(flagDir, sizeof(flagDir), "%s/" FLAG_FOLDER_PREFIX "%s",
             EXEFS_PATCHES_DIR, code);

    mkdir(EXEFS_PATCHES_DIR, 0777);
    if (mkdir(flagDir, 0777) != 0 && errno != EEXIST) {
        free(data);
        return -2;
    }

    // Write the IPS patch.
    char ipsPath[FS_MAX_PATH];
    snprintf(ipsPath, sizeof(ipsPath), "%s/" BUILD_ID ".ips", flagDir);

    FILE *f = fopen(ipsPath, "wb");
    if (!f) { free(data); return -2; }

    bool ok = (fwrite(data, 1, len, f) == len);
    fclose(f);
    free(data);

    if (!ok) { remove(ipsPath); return -2; }

    fsdevCommitDevice("sdmc");
    return 0;
}
