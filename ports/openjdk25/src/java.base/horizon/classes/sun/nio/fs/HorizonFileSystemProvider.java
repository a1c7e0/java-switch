/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */
package sun.nio.fs;

import java.io.IOException;
import java.nio.file.FileStore;

/** Uses the shared Unix file operations and reports unsupported mount metadata explicitly. */
final class HorizonFileSystemProvider extends UnixFileSystemProvider {
    @Override
    UnixFileSystem newFileSystem(String directory) {
        return new HorizonFileSystem(this, directory);
    }

    @Override
    FileStore getFileStore(UnixPath path) throws IOException {
        throw new IOException("Horizon file-store queries are not implemented");
    }
}
