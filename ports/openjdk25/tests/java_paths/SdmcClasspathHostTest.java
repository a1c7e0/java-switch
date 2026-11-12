/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */
import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Path;
import java.util.ArrayList;

/** Exercises real JAR loading with sdmc-like paths on a host JDK. */
public final class SdmcClasspathHostTest {
    public static void main(String[] args) throws Exception {
        String classPath = "sdmc:/switch/mc-runtime/tests/payload.jar;" +
                           "sdmc:/switch/mc-runtime/tests/missing.jar";
        ArrayList<URL> urls = new ArrayList<>();
        for (String entry : classPath.split(";", -1)) {
            try {
                // This is the OpenJDK URLClassPath conversion sequence.
                urls.add(Path.of(entry).toRealPath().toFile().toURI().toURL());
            } catch (java.io.IOException ignored) {
                // URLClassPath discards non-existent classpath entries.
            }
        }
        try (URLClassLoader loader = new URLClassLoader(urls.toArray(URL[]::new), null)) {
            Class<?> payload = loader.loadClass("hostclasspath.SdmcClasspathPayload");
            Object marker = payload.getMethod("marker").invoke(null);
            if (!"loaded-from-sdmc-jar".equals(marker)) {
                throw new AssertionError("Unexpected payload marker: " + marker);
            }
        }
        System.out.println("Host Java loaded a real JAR from sdmc:/ classpath URI; Horizon NOT TESTED");
    }
}
