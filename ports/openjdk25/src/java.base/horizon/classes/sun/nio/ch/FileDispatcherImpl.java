/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */
package sun.nio.ch;

import java.nio.channels.SelectableChannel;

/**
 * Uses the shared Unix file-descriptor operations provided by Horizon's
 * native dispatcher. Horizon does not provide the Unix sendfile extension,
 * so transfers use the FileChannel buffered fallback.
 */
final class FileDispatcherImpl extends UnixFileDispatcherImpl {
    FileDispatcherImpl() {
        super();
    }

    @Override
    boolean canTransferToDirectly(SelectableChannel channel) {
        return false;
    }
}
