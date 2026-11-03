#include <stdlib.h>
#include <string.h>

#include "FileSystemSupport_md.h"

char *basePath(const char *path);
int isAbsolute(const char *path);
char *normalize_path(const char *path);
char *resolve(const char *parent, const char *child);
char *fromURIPath(const char *path);
int convertUtf8ToPlatformString(char *utf8, int length, char *output, int capacity);

static int expect_string(char *actual, const char *expected, const char *original) {
    int ok = actual != NULL && strcmp(actual, expected) == 0;
    if (actual != original) free(actual);
    return ok;
}

int main(void) {
    char device_path[] = "sdmc:/mods/agent.jar";
    if (!expect_string(basePath(device_path), "sdmc:/mods", device_path)) return 1;
    if (!expect_string(basePath("sdmc:/agent.jar"), "sdmc:/", NULL)) return 2;
    if (!expect_string(basePath("/agent.jar"), "/", NULL)) return 3;
    if (!expect_string(basePath("mods/agent.jar"), "mods", NULL)) return 4;
    const char *relative = "agent.jar";
    if (basePath(relative) != relative) return 5;

    if (!isAbsolute("/agent.jar") || !isAbsolute("sdmc:/agent.jar") ||
        !isAbsolute("romfs:/agent.jar") || isAbsolute("sdmc:agent.jar") ||
        isAbsolute("mods/agent.jar")) return 6;
    if (pathSeparator != ';') return 7;

    const char *normal = "mods/agent.jar";
    if (normalize_path(normal) != normal) return 8;
    const char *slash_root = "/";
    const char *device_root = "sdmc:/";
    if (normalize_path(slash_root) != slash_root ||
        normalize_path(device_root) != device_root) return 9;
    if (!expect_string(normalize_path("//agent.jar///"), "/agent.jar", NULL)) return 10;
    if (!expect_string(normalize_path("sdmc://mods//agent.jar/"),
                       "sdmc:/mods/agent.jar", NULL)) return 11;
    if (!expect_string(normalize_path("mods//agent.jar/"),
                       "mods/agent.jar", NULL)) return 12;
    if (!expect_string(normalize_path("romfs://"), "romfs:/", NULL)) return 13;

    if (!expect_string(resolve("sdmc:/mods/", "boot/agent.jar"),
                       "sdmc:/mods/boot/agent.jar", NULL)) return 14;
    if (!expect_string(resolve("sdmc:/", "boot.jar"), "sdmc:/boot.jar", NULL)) return 15;
    if (!expect_string(resolve("sdmc:/mods", "romfs:/boot.jar"),
                       "romfs:/boot.jar", NULL)) return 16;
    if (!expect_string(resolve("sdmc:/mods", "/boot.jar"), "/boot.jar", NULL)) return 17;
    if (!expect_string(resolve("", "boot.jar"), "boot.jar", NULL)) return 18;
    if (!expect_string(resolve("mods", ""), "mods", NULL)) return 19;

    if (!expect_string(fromURIPath("/sdmc:/mods/agent.jar/"),
                       "sdmc:/mods/agent.jar", NULL)) return 20;
    if (!expect_string(fromURIPath("/sdmc:/"), "sdmc:/", NULL)) return 21;
    if (!expect_string(fromURIPath("/agent.jar/"), "/agent.jar", NULL)) return 22;
    const char *uri_path = "agent.jar";
    if (fromURIPath(uri_path) != uri_path) return 23;

    char output[32];
    char utf8[] = "agent-\xc3\xa9.jar";
    int length = (int)strlen(utf8);
    if (convertUtf8ToPlatformString(utf8, length, output, sizeof(output)) != length ||
        strcmp(output, utf8) != 0) return 24;
    char utf8_supplementary[] = "\xf0\x9f\x98\x80";
    if (convertUtf8ToPlatformString(utf8_supplementary, 4, output, sizeof(output)) != 4 ||
        memcmp(output, utf8_supplementary, 5) != 0) return 25;
    char modified_utf8_pair[] = "\xed\xa0\xbd\xed\xb8\x80";
    if (convertUtf8ToPlatformString(modified_utf8_pair, 6, output, sizeof(output)) != 4 ||
        memcmp(output, utf8_supplementary, 5) != 0) return 26;
    char isolated_surrogate[] = "\xed\xa0\xbd";
    if (convertUtf8ToPlatformString(isolated_surrogate, 3, output, sizeof(output)) != -1) return 27;
    char modified_nul[] = "\xc0\x80";
    if (convertUtf8ToPlatformString(modified_nul, 2, output, sizeof(output)) != -1) return 28;
    if (convertUtf8ToPlatformString(utf8, length, output, length) != -1) return 29;
    if (convertUtf8ToPlatformString(utf8, -1, output, sizeof(output)) != -1) return 30;
    if (convertUtf8ToPlatformString(NULL, 0, output, sizeof(output)) != -1) return 31;
    return 0;
}
