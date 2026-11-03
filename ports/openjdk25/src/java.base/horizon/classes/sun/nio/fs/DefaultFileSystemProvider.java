/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */
package sun.nio.fs;

import java.nio.file.FileSystem;

/** Selects the common Unix/newlib-backed provider without identifying as Linux. */
public final class DefaultFileSystemProvider {
    private static final HorizonFileSystemProvider INSTANCE = new HorizonFileSystemProvider();

    private DefaultFileSystemProvider() { }

    public static HorizonFileSystemProvider instance() {
        return INSTANCE;
    }

    public static FileSystem theFileSystem() {
        return INSTANCE.theFileSystem();
    }
}
