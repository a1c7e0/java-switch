/*
 * Copyright (c) 2002, 2013, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */
package sun.security.provider;

import java.io.IOException;
import java.security.ProviderException;

/** Seed generator backed by the Horizon CSRNG service. */
class NativeSeedGenerator extends SeedGenerator {

    NativeSeedGenerator(String seedFile) throws IOException {
        super();
        if (!SunEntries.URL_DEV_RANDOM.equals(seedFile) &&
                !SunEntries.URL_DEV_URANDOM.equals(seedFile)) {
            String detail = "Horizon NativeSeedGenerator does not support source " +
                    seedFile;
            throw new ProviderException(detail, new IOException(detail));
        }
        long result = nativeGenerateSeed(new byte[2]);
        if (result != 0) {
            /* ProviderException escapes SeedGenerator's IOException fallback,
             * so a failed OS CSRNG never silently selects timed seed material. */
            throw failure("initialization", result);
        }
    }

    @Override
    void getSeedBytes(byte[] result) {
        long rawResult = nativeGenerateSeed(result);
        if (rawResult != 0) {
            throw failure("seed generation", rawResult);
        }
    }

    private static ProviderException failure(String operation, long rawResult) {
        String detail = "Horizon CSRNG " + operation + " failed (Result=0x" +
                Long.toHexString(rawResult & 0xffff_ffffL) + ")";
        return new ProviderException(detail, new IOException(detail));
    }

    /** Returns zero on success or the unsigned 32-bit libnx Result. */
    private static native long nativeGenerateSeed(byte[] result);
}
