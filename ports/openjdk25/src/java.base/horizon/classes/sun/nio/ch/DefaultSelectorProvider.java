/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */
package sun.nio.ch;

/** Selects the common poll-based selector for Horizon. */
public final class DefaultSelectorProvider {
    private static final SelectorProviderImpl INSTANCE = new PollSelectorProvider();

    private DefaultSelectorProvider() { }

    public static SelectorProviderImpl get() {
        return INSTANCE;
    }
}
