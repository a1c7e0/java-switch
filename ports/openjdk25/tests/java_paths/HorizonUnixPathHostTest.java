/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */
package sun.nio.fs;

import java.io.File;
import java.net.URI;
import java.lang.reflect.Field;
import java.nio.charset.Charset;
import java.nio.file.FileSystem;
import java.nio.file.Path;
import jdk.internal.util.StaticProperty;

/** Host-only path semantics check; excluded from the product java.base module. */
public final class HorizonUnixPathHostTest {
    private static void check(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    private static void equal(Object actual, Object expected, String message) {
        check(expected.equals(actual), message + ": expected " + expected + ", got " + actual);
    }

    public static void main(String[] args) throws Exception {
        File deviceFile = new File("sdmc:/alpha/beta");
        check(deviceFile.isAbsolute(), "java.io.File must treat sdmc:/ as absolute");
        equal(deviceFile.getAbsolutePath(), "sdmc:/alpha/beta", "java.io absolute path");
        equal(new File("sdmc://alpha//beta").getPath(), "sdmc:/alpha/beta",
                "java.io duplicate root slashes");
        equal(new File("sdmc:/").getPath(), "sdmc:/", "java.io device root normalization");
        equal(new File("sdmc:////alpha///beta//").getPath(), "sdmc:/alpha/beta",
                "java.io duplicate and trailing device-root slashes");
        equal(File.listRoots()[0].getPath(), "sdmc:/", "java.io root list");

        Field processCharset = Class.forName("java.lang.ProcessImpl")
                .getDeclaredField("JNU_CHARSET");
        processCharset.setAccessible(true);
        equal(processCharset.get(null), Charset.forName(StaticProperty.jnuEncoding()),
                "ProcessEnvironment-compatible JNU charset");

        HorizonFileSystemProvider provider = new HorizonFileSystemProvider();
        FileSystem fs = provider.theFileSystem();
        Path path = fs.getPath("sdmc:/alpha/beta");
        check(path.isAbsolute(), "NIO path must treat sdmc:/ as absolute");
        equal(path.getRoot().toString(), "sdmc:/", "NIO root");
        equal(path.getRoot().getNameCount(), 0, "root has no names");
        equal(path.getNameCount(), 2, "device prefix is not a name");
        equal(path.getName(0).toString(), "alpha", "first path name");
        equal(path.getFileName().toString(), "beta", "file name");
        equal(path.getParent().toString(), "sdmc:/alpha", "parent path");
        equal(path.getParent().getParent().toString(), "sdmc:/", "root parent");
        equal(fs.getPath("sdmc://alpha//beta///").toString(), "sdmc:/alpha/beta",
                "NIO duplicate and trailing device-root slashes");
        equal(fs.getPath("sdmc:/alpha/./beta/../gamma").normalize().toString(),
                "sdmc:/alpha/gamma", "normalize with device root");
        equal(fs.getPath("sdmc:/../../gamma").normalize().toString(),
                "sdmc:/gamma", "normalize cannot climb above device root");
        equal(fs.getPath("sdmc:/alpha").resolve("beta").toString(),
                "sdmc:/alpha/beta", "resolve relative child");
        equal(fs.getPath("sdmc:/").resolve("alpha").toString(), "sdmc:/alpha",
                "resolve from device root");
        equal(fs.getPath("sdmc:/alpha").resolve(fs.getPath("/beta")).toString(),
                "sdmc:/beta", "resolve absolute device child");
        equal(fs.getPath("sdmc:/alpha").relativize(fs.getPath("sdmc:/alpha/beta")).toString(),
                "beta", "relativize");
        check(path.startsWith(fs.getPath("sdmc:/alpha")), "startsWith path prefix");
        check(path.startsWith(fs.getPath("sdmc:/")), "startsWith device root");
        check(!path.endsWith(fs.getPath("sdmc:/alpha")), "endsWith rejects non-suffix");
        check(path.endsWith(fs.getPath("alpha/beta")), "endsWith relative suffix");
        Path absolute = fs.getPath("relative").toAbsolutePath();
        check(absolute.isAbsolute(), "toAbsolutePath");
        equal(absolute.getRoot().toString(), "sdmc:/", "toAbsolutePath device root");

        URI uri = path.toUri();
        equal(uri.getRawPath(), "/sdmc:/alpha/beta", "device URI encoding");
        equal(provider.getPath(uri).toString(), path.toString(), "URI round trip");
        equal(provider.getPath(URI.create("file:///alpha/beta")).toString(),
                "sdmc:/alpha/beta", "generic file URI maps to Horizon device root");
        equal(fs.getPath("/alpha").toString(), "sdmc:/alpha", "slash root maps to device root");
        System.out.println("Horizon UnixPath host test: PASS");
    }
}
