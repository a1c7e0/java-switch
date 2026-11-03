/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */
package sun.nio.fs;

import java.io.IOException;
import java.nio.file.FileStore;
import java.nio.file.WatchService;
import java.util.Set;

/** Shared Unix path operations with explicit failure for unavailable mount metadata. */
final class HorizonFileSystem extends UnixFileSystem {
    HorizonFileSystem(UnixFileSystemProvider provider, String directory) {
        super(provider, directory);
    }

    @Override
    public WatchService newWatchService() throws IOException {
        throw new IOException("Horizon file watching is not implemented");
    }

    @Override
    public Set<String> supportedFileAttributeViews() {
        return Set.copyOf(standardFileAttributeViews());
    }

    @Override
    Iterable<UnixMountEntry> getMountEntries() {
        throw new UnsupportedOperationException("Horizon mount enumeration is not implemented");
    }

    @Override
    FileStore getFileStore(UnixMountEntry entry) throws IOException {
        throw new IOException("Horizon file-store queries are not implemented");
    }
}
