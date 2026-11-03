/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */
package sun.nio.ch;

import java.io.IOException;

/** Fails explicitly until Horizon has a Poller backend for virtual-thread I/O. */
final class DefaultPollerProvider extends PollerProvider {
    @Override
    Poller readPoller(boolean subPoller) throws IOException {
        throw new IOException("Horizon virtual-thread read polling is not implemented");
    }

    @Override
    Poller writePoller(boolean subPoller) throws IOException {
        throw new IOException("Horizon virtual-thread write polling is not implemented");
    }
}
