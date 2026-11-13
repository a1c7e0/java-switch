/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
public final class HelloClasspathPayload {
    private HelloClasspathPayload() {}

    public static String marker() {
        return "loaded-default-package-class";
    }
}
