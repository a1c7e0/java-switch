/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */
package sun.nio.ch;

import java.io.IOException;
import java.nio.channels.AsynchronousChannelGroup;
import java.nio.channels.AsynchronousServerSocketChannel;
import java.nio.channels.AsynchronousSocketChannel;
import java.nio.channels.spi.AsynchronousChannelProvider;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.ThreadFactory;

/** No asynchronous channel backend is available on Horizon yet. */
public final class DefaultAsynchronousChannelProvider {
    private DefaultAsynchronousChannelProvider() { }

    public static AsynchronousChannelProvider create() {
        return new HorizonAsynchronousChannelProvider();
    }

    private static final class HorizonAsynchronousChannelProvider
            extends AsynchronousChannelProvider {
        @Override
        public AsynchronousChannelGroup openAsynchronousChannelGroup(
                int threads, ThreadFactory factory) throws IOException {
            throw unsupported();
        }

        @Override
        public AsynchronousChannelGroup openAsynchronousChannelGroup(
                ExecutorService executor, int initialSize) throws IOException {
            throw unsupported();
        }

        @Override
        public AsynchronousServerSocketChannel openAsynchronousServerSocketChannel(
                AsynchronousChannelGroup group) throws IOException {
            throw unsupported();
        }

        @Override
        public AsynchronousSocketChannel openAsynchronousSocketChannel(
                AsynchronousChannelGroup group) throws IOException {
            throw unsupported();
        }

        private IOException unsupported() {
            return new IOException("Horizon asynchronous channels are not implemented");
        }
    }
}
