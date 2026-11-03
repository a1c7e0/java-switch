/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */
package java.lang;

import java.io.IOException;
import java.nio.charset.Charset;
import java.util.Map;
import jdk.internal.util.StaticProperty;

/** Horizon does not provide the POSIX process creation ABI used by Unix ProcessImpl. */
final class ProcessImpl {
    static final boolean SUPPORTS_NORMAL_TERMINATION = false;
    // Keep the shared Unix ProcessEnvironment byte encoding policy.
    static final Charset JNU_CHARSET = Charset.forName(StaticProperty.jnuEncoding());

    private ProcessImpl() { }

    static Process start(String[] command, Map<String, String> environment,
                         String directory, ProcessBuilder.Redirect[] redirects,
                         boolean redirectErrorStream) throws IOException {
        throw new IOException("Process creation is not implemented on Horizon");
    }
}
