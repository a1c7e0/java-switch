/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */
import java.net.URL;
import java.net.URI;
import java.net.URLClassLoader;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;

/** Exercises URLClassPath's maintained FileURLMapper on a host JDK. */
public final class SdmcClasspathHostTest {
    public static void main(String[] args) throws Exception {
        String deviceJar = "file:/sdmc%3A/switch/mc%20runtime/%E6%B8%AC%E8%A9%A6/payload.jar";
        URL jarUrl = URI.create(deviceJar).toURL();
        if (args.length != 0 && args[0].equals("expect-cnfe")) {
            try (URLClassLoader loader = new URLClassLoader(new URL[] {jarUrl}, null)) {
                loader.loadClass("hostclasspath.SdmcClasspathPayload");
                throw new AssertionError("unpatched mapper unexpectedly loaded SD JAR");
            } catch (ClassNotFoundException expected) {
                if (!expected.getMessage().equals("hostclasspath.SdmcClasspathPayload")) {
                    throw new AssertionError("Unexpected CNFE: " + expected);
                }
                System.out.println("Unpatched class loader reproduces payload CNFE");
                return;
            }
        }
        ArrayList<URL> urls = new ArrayList<>();
        urls.add(jarUrl);
        urls.add(URI.create("file://localhost/sdmc%3A/switch/mc%20runtime/%E6%B8%AC%E8%A9%A6/payload.jar").toURL());
        try (URLClassLoader loader = new URLClassLoader(urls.toArray(URL[]::new), null)) {
            Class<?> payload = loader.loadClass("hostclasspath.SdmcClasspathPayload");
            Object marker = payload.getMethod("marker").invoke(null);
            if (!"loaded-from-sdmc-jar".equals(marker)) {
                throw new AssertionError("Unexpected payload marker: " + marker);
            }
            Class<?> hello = loader.loadClass("HelloClasspathPayload");
            Object helloMarker = hello.getMethod("marker").invoke(null);
            if (!"loaded-default-package-class".equals(helloMarker)) {
                throw new AssertionError("Unexpected default-package marker: " + helloMarker);
            }
            if (loader.findResource("missing-resource.txt") != null) {
                throw new AssertionError("Unexpected missing resource");
            }
            equalResource(loader.getResourceAsStream("payload.txt"));
        }
        URL directoryUrl = URI.create("file:/" +
                "sdmc%3A/switch/mc%20runtime/%E6%B8%AC%E8%A9%A6/classes/").toURL();
        try (URLClassLoader loader = new URLClassLoader(new URL[] {directoryUrl}, null)) {
            Class<?> payload = loader.loadClass("hostclasspath.SdmcClasspathPayload");
            Object marker = payload.getMethod("marker").invoke(null);
            if (!"loaded-from-sdmc-jar".equals(marker)) {
                throw new AssertionError("Unexpected directory payload marker: " + marker);
            }
            equalResource(loader.getResourceAsStream("payload.txt"));
        }
        URL jarResource = URI.create("jar:" + deviceJar + "!/payload.txt").toURL();
        equalResource(jarResource.openStream());
        URL fileResource = URI.create("file:/sdmc%3A/switch/mc%20runtime/%E6%B8%AC%E8%A9%A6/classes/payload.txt").toURL();
        equalResource(fileResource.openStream());
        URL unixJar = Path.of(args[0]).toUri().toURL();
        try (URLClassLoader loader = new URLClassLoader(new URL[] {unixJar}, null)) {
            loader.loadClass("hostclasspath.SdmcClasspathPayload");
        }
        URL remoteUrl = URI.create("file://example.invalid/sdmc%3A/switch/mc%20runtime/%E6%B8%AC%E8%A9%A6/payload.jar").toURL();
        try (URLClassLoader loader = new URLClassLoader(new URL[] {remoteUrl}, null)) {
            try {
                loader.loadClass("hostclasspath.SdmcClasspathPayload");
                throw new AssertionError("remote file URL unexpectedly loaded a class");
            } catch (ClassNotFoundException expected) {
                if (!expected.getMessage().equals("hostclasspath.SdmcClasspathPayload")) {
                    throw new AssertionError("Unexpected remote URL CNFE: " + expected);
                }
            }
        }
        URL remoteDirectory = URI.create("file://example.invalid/sdmc%3A/switch/mc%20runtime/%E6%B8%AC%E8%A9%A6/classes/").toURL();
        try (URLClassLoader loader = new URLClassLoader(new URL[] {remoteDirectory}, null)) {
            try {
                loader.loadClass("hostclasspath.SdmcClasspathPayload");
                throw new AssertionError("remote file directory unexpectedly loaded a class");
            } catch (ClassNotFoundException expected) {
                if (!expected.getMessage().equals("hostclasspath.SdmcClasspathPayload")) {
                    throw new AssertionError("Unexpected remote directory CNFE: " + expected);
                }
            }
        }
        System.out.println("Maintained Horizon URLClassPath loaded encoded device JAR and directory classes; JAR and file URL resource streams passed; localhost/Unix paths passed; remote URLs and missing resource stayed absent; Switch NOT TESTED");
    }

    private static void equalResource(java.io.InputStream stream) throws Exception {
        if (stream == null) {
            throw new AssertionError("Expected resource stream");
        }
        try (stream) {
            String content = new String(stream.readAllBytes(), StandardCharsets.UTF_8);
            if (!"resource-through-device-path\n".equals(content)) {
                throw new AssertionError("Unexpected resource content: " + content);
            }
        }
    }
}
