/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Horizon implementation of the OpenJDK java_props.h platform interface.
 * The structure and call contract come from the pinned OpenJDK 25 source;
 * platform values below are selected for the libnx/newlib runtime.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef ARCHPROPNAME
#error "The macro ARCHPROPNAME has not been defined"
#endif

#include "java_props.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Horizon's standard application install area is on the SD card. Applications
 * may override this with TMPDIR, but must create the directory themselves.
 * This gives temporary files a writable, explicit location without assuming
 * that a Unix /tmp mount exists.
 */
#define HORIZON_DEFAULT_TMPDIR "sdmc:/switch/mc-runtime/tmp"

java_props_t *
GetJavaProperties(JNIEnv *env)
{
    static java_props_t sprops;
    static char current_dir[PATH_MAX];

    if (sprops.user_dir != NULL) {
        return &sprops;
    }

    const char *tmp_dir = getenv("TMPDIR");
    sprops.tmp_dir = (nchar *)((tmp_dir != NULL && tmp_dir[0] != '\0')
                                   ? tmp_dir : HORIZON_DEFAULT_TMPDIR);

    /* The native file and path APIs use UTF-8 bytes through newlib/libnx.
     * Locale services are not available here, so do not infer en_US (or any
     * other user locale) from the C locale. Null locale fields are intentionally
     * omitted by OpenJDK's property builder.
     */
    sprops.encoding = "UTF-8";
    sprops.sun_jnu_encoding = "UTF-8";
    sprops.stdin_encoding = NULL;
    sprops.stdout_encoding = NULL;
    sprops.stderr_encoding = NULL;
    sprops.format_language = NULL;
    sprops.display_language = NULL;
    sprops.format_script = NULL;
    sprops.display_script = NULL;
    sprops.format_country = NULL;
    sprops.display_country = NULL;
    sprops.format_variant = NULL;
    sprops.display_variant = NULL;

    sprops.os_name = "Horizon";
    /* libnx can report firmware after runtime initialization, but java.base
     * does not own that initialization contract. Keep os.version explicitly
     * unavailable rather than reporting the build host or inventing a version.
     */
    sprops.os_version = "unknown";
    sprops.os_arch = ARCHPROPNAME;
    sprops.patch_level = NULL;
    sprops.cpu_isalist = NULL;

    {
        const uint16_t endian_test = 1;
        sprops.cpu_endian = (*(const uint8_t *)&endian_test == 1)
                                ? "little" : "big";
        sprops.unicode_encoding = (*(const uint8_t *)&endian_test == 1)
                                      ? "UnicodeLittle" : "UnicodeBig";
    }

    /* libnx does not provide Unix passwd/locale databases. Respect explicit
     * app-provided identity environment variables when present, and leave the
     * values when supplied. StaticProperty requires non-null user.name/home;
     * '?' is an explicit unknown identity and the SD runtime root is the
     * application-home policy, not a claimed Horizon account directory.
     */
    sprops.user_name = (nchar *)getenv("USER");
    if (sprops.user_name == NULL || sprops.user_name[0] == '\0') {
        sprops.user_name = (nchar *)getenv("LOGNAME");
    }
    if (sprops.user_name == NULL || sprops.user_name[0] == '\0') {
        sprops.user_name = (nchar *)"?";
    }
    sprops.user_home = (nchar *)getenv("HOME");
    if (sprops.user_home == NULL || sprops.user_home[0] == '\0') {
        sprops.user_home = (nchar *)"sdmc:/switch/mc-runtime";
    }

    if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
        JNU_ThrowByName(env, "java/lang/Error",
                        "Properties init: Could not determine current working directory.");
        return NULL;
    }
    sprops.user_dir = current_dir;

    sprops.file_separator = "/";
    /* Horizon storage mount names include ':', so ':' cannot separate paths. */
    sprops.path_separator = ";";
    sprops.line_separator = "\n";

    return &sprops;
}

jstring
GetStringPlatform(JNIEnv *env, nchar *cstr)
{
    /* Horizon native paths and environment strings are UTF-8. */
    return JNU_NewStringPlatform(env, cstr);
}
