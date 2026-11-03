/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "java_props.h"

void JNU_ThrowByName(JNIEnv *env, const char *name, const char *message) {
    (void)env;
    fprintf(stderr, "unexpected Java exception %s: %s\n", name, message);
    abort();
}

jstring JNU_NewStringPlatform(JNIEnv *env, const char *value) {
    (void)env;
    (void)value;
    return NULL;
}

int main(int argc, char **argv) {
    assert(argc == 4);
    java_props_t *props = GetJavaProperties(NULL);
    assert(props != NULL);
    /* Upstream StaticProperty throws InternalError if any required value is
       absent. Exercise this bootstrap contract even without a Unix identity. */
    assert(props->user_name != NULL && props->user_home != NULL);
    assert(props->user_dir != NULL && props->encoding != NULL);
    assert(props->sun_jnu_encoding != NULL && props->os_name != NULL);
    assert(props->os_arch != NULL && props->os_version != NULL);
    assert(strcmp(props->os_name, "Horizon") == 0);
    assert(strcmp(props->os_version, "unknown") == 0);
    assert(strcmp(props->os_arch, "aarch64") == 0);
    assert(strcmp((char *)props->tmp_dir, argv[1]) == 0);
    assert(strcmp(props->file_separator, "/") == 0);
    assert(strcmp(props->path_separator, ";") == 0);
    assert(strcmp(props->line_separator, "\n") == 0);
    assert(strcmp(props->encoding, "UTF-8") == 0);
    assert(strcmp(props->sun_jnu_encoding, "UTF-8") == 0);
    assert(props->format_language == NULL && props->display_language == NULL);
    assert(props->format_country == NULL && props->display_country == NULL);
    assert(strcmp(props->user_name == NULL ? "" : (char *)props->user_name,
                  argv[2]) == 0);
    assert(strcmp(props->user_home == NULL ? "" : (char *)props->user_home,
                  argv[3]) == 0);

    char current_dir[4096];
    assert(getcwd(current_dir, sizeof(current_dir)) != NULL);
    assert(strcmp((char *)props->user_dir, current_dir) == 0);
    assert(GetJavaProperties(NULL) == props);  /* initialized once */
    return 0;
}
