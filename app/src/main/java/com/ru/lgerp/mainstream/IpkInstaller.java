package com.ru.lgerp.mainstream;

import android.content.Context;
import android.net.Uri;
import android.util.Log;

import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.EOFException;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.zip.GZIPInputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public class IpkInstaller {
    private static final String TAG = "IpkInstaller";
    private static final String GAMES_DIR = "games";

    public static class GameEntry {
        public final String id;
        public final String title;
        public final String main;
        public final String icon;
        public final int width;
        public final int height;
        public final File dir;

        public GameEntry(String id, String title, String main, String icon, int width, int height, File dir) {
            this.id = id;
            this.title = title;
            this.main = main;
            this.icon = icon;
            this.width = width;
            this.height = height;
            this.dir = dir;
        }
    }

    public static File getGamesRoot(Context context) {
        File root = new File(context.getFilesDir(), GAMES_DIR);
        if (!root.exists()) {
            root.mkdirs();
        }
        return root;
    }

    public static List<GameEntry> listInstalledGames(Context context) {
        List<GameEntry> list = new ArrayList<>();
        File root = getGamesRoot(context);
        File[] dirs = root.listFiles();
        if (dirs == null) return list;

        for (File dir : dirs) {
            if (!dir.isDirectory()) continue;
            File appInfoFile = new File(dir, "appinfo.json");
            if (!appInfoFile.exists()) continue;
            GameEntry entry = parseAppInfo(dir, appInfoFile);
            if (entry != null) {
                list.add(entry);
            }
        }
        Collections.sort(list, new Comparator<GameEntry>() {
            @Override
            public int compare(GameEntry a, GameEntry b) {
                return a.title.compareToIgnoreCase(b.title);
            }
        });
        return list;
    }

    public static void uninstallGame(Context context, String id) {
        File dir = new File(getGamesRoot(context), id);
        if (dir.exists()) {
            deleteRecursive(dir);
        }
    }

    public static GameEntry installFromUri(Context context, Uri uri) throws IOException {
        File staging = new File(context.getCacheDir(), "ipk_staging_" + System.currentTimeMillis());
        if (staging.exists()) deleteRecursive(staging);
        staging.mkdirs();

        try (InputStream raw = context.getContentResolver().openInputStream(uri)) {
            if (raw == null) {
                throw new IOException("Cannot open selected IPK file");
            }
            BufferedInputStream bis = new BufferedInputStream(raw, 65536);
            bis.mark(16);
            byte[] magic = new byte[8];
            int n = readFullyOrLess(bis, magic, 8);
            bis.reset();

            if (n >= 7 && new String(magic, 0, 7, StandardCharsets.US_ASCII).equals("!<arch>")) {
                extractDataFromAr(bis, staging);
            } else if (n >= 2 && (magic[0] & 0xFF) == 0x1F && (magic[1] & 0xFF) == 0x8B) {
                extractTar(new GZIPInputStream(bis), staging);
            } else if (n >= 4 && magic[0] == 'P' && magic[1] == 'K' && magic[2] == 3 && magic[3] == 4) {
                extractZip(bis, staging);
            } else {
                throw new IOException("Unsupported archive format (expected .ipk, .tar.gz, or .zip)");
            }

            File appDir = findAppDir(staging);
            if (appDir == null) {
                appDir = findNetCastAppDirAndSynthesize(staging);
            }
            if (appDir == null) {
                throw new IOException("appinfo.json or NetCast ELF binary not found inside archive");
            }
            File appInfoFile = new File(appDir, "appinfo.json");
            GameEntry parsed = parseAppInfo(appDir, appInfoFile);
            if (parsed == null || parsed.id.isEmpty() || parsed.main.isEmpty()) {
                throw new IOException("Invalid appinfo.json (missing id or main)");
            }

            File destDir = new File(getGamesRoot(context), sanitizeId(parsed.id));
            if (destDir.exists()) {
                deleteRecursive(destDir);
            }
            if (!appDir.renameTo(destDir)) {
                copyRecursive(appDir, destDir);
            }

            File mainBin = new File(destDir, parsed.main);
            if (mainBin.exists()) {
                mainBin.setExecutable(true, false);
            }
            return parseAppInfo(destDir, new File(destDir, "appinfo.json"));
        } finally {
            deleteRecursive(staging);
        }
    }

    private static void extractDataFromAr(InputStream in, File destDir) throws IOException {
        byte[] magic = new byte[8];
        readFully(in, magic, 8);
        byte[] hdr = new byte[60];
        boolean foundData = false;

        while (true) {
            int r = readFullyOrLess(in, hdr, 60);
            if (r == 0) break;
            if (r < 60) throw new EOFException("Truncated ar header");

            String name = new String(hdr, 0, 16, StandardCharsets.US_ASCII).trim();
            if (name.endsWith("/")) name = name.substring(0, name.length() - 1);
            String sizeStr = new String(hdr, 48, 10, StandardCharsets.US_ASCII).trim();
            long size = Long.parseLong(sizeStr);

            if (name.startsWith("#1/")) {
                int extLen = Integer.parseInt(name.substring(3));
                byte[] extName = new byte[extLen];
                readFully(in, extName, extLen);
                name = new String(extName, StandardCharsets.UTF_8).replace("\0", "").trim();
                size -= extLen;
            }

            Log.i(TAG, "ar entry: " + name + " (" + size + " bytes)");
            if (name.equals("data.tar.gz") || name.equals("data.tgz")) {
                InputStream limited = new BoundedInputStream(in, size);
                extractTar(new GZIPInputStream(limited), destDir);
                foundData = true;
                break;
            } else if (name.equals("data.tar")) {
                InputStream limited = new BoundedInputStream(in, size);
                extractTar(limited, destDir);
                foundData = true;
                break;
            } else {
                skipFully(in, size);
            }

            if ((size & 1L) != 0) {
                skipFully(in, 1);
            }
        }

        if (!foundData) {
            throw new IOException("No data.tar.gz found inside IPK");
        }
    }

    private static void extractTar(InputStream in, File destDir) throws IOException {
        byte[] hdr = new byte[512];
        byte[] copyBuf = new byte[32768];
        String nextLongName = null;

        while (true) {
            int r = readFullyOrLess(in, hdr, 512);
            if (r == 0) break;
            if (r < 512) break;
            if (isZeroBlock(hdr)) break;

            String name = readTarString(hdr, 0, 100);
            String prefix = readTarString(hdr, 345, 155);
            if (!prefix.isEmpty()) {
                name = prefix + "/" + name;
            }
            long size = parseOctalOrBase256(hdr, 124, 12);
            byte typeFlag = hdr[156];

            if (typeFlag == 'L') {
                byte[] longBytes = new byte[(int) size];
                readFully(in, longBytes, (int) size);
                nextLongName = new String(longBytes, StandardCharsets.UTF_8).replace("\0", "").trim();
                skipTarPadding(in, size);
                continue;
            }

            if (nextLongName != null) {
                name = nextLongName;
                nextLongName = null;
            }

            if (typeFlag == 'x' || typeFlag == 'g') {
                skipFully(in, size);
                skipTarPadding(in, size);
                continue;
            }

            String clean = normalizeTarPath(name);
            if (clean == null || clean.isEmpty()) {
                skipFully(in, size);
                skipTarPadding(in, size);
                continue;
            }

            File outFile = new File(destDir, clean);
            String canonicalDest = destDir.getCanonicalPath();
            String canonicalOut = outFile.getCanonicalPath();
            if (!canonicalOut.startsWith(canonicalDest + File.separator) && !canonicalOut.equals(canonicalDest)) {
                throw new IOException("Blocked path traversal in tar: " + name);
            }

            if (typeFlag == '5' || name.endsWith("/")) {
                outFile.mkdirs();
                skipFully(in, size);
                skipTarPadding(in, size);
            } else if (typeFlag == '0' || typeFlag == 0) {
                File parent = outFile.getParentFile();
                if (parent != null && !parent.exists()) parent.mkdirs();
                try (FileOutputStream fos = new FileOutputStream(outFile)) {
                    long remaining = size;
                    while (remaining > 0) {
                        int toRead = (int) Math.min(copyBuf.length, remaining);
                        int read = in.read(copyBuf, 0, toRead);
                        if (read < 0) throw new EOFException("Unexpected EOF in tar entry " + name);
                        fos.write(copyBuf, 0, read);
                        remaining -= read;
                    }
                }
                outFile.setExecutable(true, false);
                skipTarPadding(in, size);
            } else {
                skipFully(in, size);
                skipTarPadding(in, size);
            }
        }
    }

    private static void extractZip(InputStream in, File destDir) throws IOException {
        byte[] copyBuf = new byte[32768];
        String canonicalDest = destDir.getCanonicalPath();
        try (ZipInputStream zis = new ZipInputStream(in)) {
            ZipEntry entry;
            while ((entry = zis.getNextEntry()) != null) {
                String clean = normalizeTarPath(entry.getName());
                if (clean.isEmpty()) continue;
                File outFile = new File(destDir, clean);
                String canonicalOut = outFile.getCanonicalPath();
                if (!canonicalOut.startsWith(canonicalDest + File.separator) && !canonicalOut.equals(canonicalDest)) {
                    throw new IOException("Blocked path traversal in zip: " + entry.getName());
                }
                if (entry.isDirectory()) {
                    outFile.mkdirs();
                } else {
                    File parent = outFile.getParentFile();
                    if (parent != null && !parent.exists()) parent.mkdirs();
                    try (FileOutputStream fos = new FileOutputStream(outFile)) {
                        int r;
                        while ((r = zis.read(copyBuf)) > 0) {
                            fos.write(copyBuf, 0, r);
                        }
                    }
                    outFile.setExecutable(true, false);
                }
                zis.closeEntry();
            }
        }
    }

    private static boolean isElfBinary(File f) {
        if (!f.isFile() || f.length() < 52) return false;
        try (FileInputStream fis = new FileInputStream(f)) {
            byte[] hdr = new byte[4];
            if (fis.read(hdr) != 4) return false;
            return hdr[0] == 0x7F && hdr[1] == 'E' && hdr[2] == 'L' && hdr[3] == 'F';
        } catch (Exception e) {
            return false;
        }
    }

    private static File findNetCastAppDirAndSynthesize(File root) {
        File[] files = root.listFiles();
        if (files == null) return null;
        File elfFile = null;
        File iconFile = null;
        for (File f : files) {
            if (f.isFile() && !f.getName().endsWith(".so") && isElfBinary(f)) {
                elfFile = f;
            } else if (f.isFile() && f.getName().toLowerCase().endsWith(".png")) {
                if (iconFile == null || f.getName().contains("130")) {
                    iconFile = f;
                }
            }
        }
        if (elfFile != null) {
            try {
                JSONObject obj = new JSONObject();
                String name = elfFile.getName();
                obj.put("id", "netcast." + sanitizeId(name));
                obj.put("title", name);
                obj.put("main", name);
                if (iconFile != null) obj.put("icon", iconFile.getName());
                obj.put("resolution", "1920x1080");
                try (FileOutputStream fos = new FileOutputStream(new File(root, "appinfo.json"))) {
                    fos.write(obj.toString(2).getBytes(StandardCharsets.UTF_8));
                }
                return root;
            } catch (Exception e) {
                Log.e(TAG, "Failed to synthesize appinfo.json", e);
            }
        }
        for (File f : files) {
            if (f.isDirectory()) {
                File found = findNetCastAppDirAndSynthesize(f);
                if (found != null) return found;
            }
        }
        return null;
    }

    private static File findAppDir(File root) {
        File direct = new File(root, "appinfo.json");
        if (direct.exists()) return root;
        File[] children = root.listFiles();
        if (children == null) return null;
        for (File child : children) {
            if (child.isDirectory()) {
                File found = findAppDir(child);
                if (found != null) return found;
            }
        }
        return null;
    }

    private static GameEntry parseAppInfo(File dir, File appInfoFile) {
        try {
            byte[] data = readFileBytes(appInfoFile);
            JSONObject obj = new JSONObject(new String(data, StandardCharsets.UTF_8));
            String id = obj.optString("id", dir.getName());
            String title = obj.optString("title", id);
            String main = obj.optString("main", "");
            String icon = obj.optString("icon", "");
            if (icon.isEmpty()) icon = obj.optString("largeIcon", "");

            int w = 1920, h = 1080;
            String res = obj.optString("resolution", "");
            if (res.isEmpty() && obj.has("vendorExtension")) {
                JSONObject ve = obj.optJSONObject("vendorExtension");
                if (ve != null) res = ve.optString("resolution", "");
            }
            if (res.contains("x")) {
                String[] parts = res.split("x");
                if (parts.length == 2) {
                    try {
                        w = Integer.parseInt(parts[0].trim());
                        h = Integer.parseInt(parts[1].trim());
                    } catch (NumberFormatException ignored) {}
                }
            }
            return new GameEntry(id, title, main, icon, w, h, dir);
        } catch (Exception e) {
            Log.e(TAG, "Failed to parse " + appInfoFile, e);
            return null;
        }
    }

    private static String normalizeTarPath(String path) {
        while (path.startsWith("./")) path = path.substring(2);
        while (path.startsWith("/")) path = path.substring(1);
        return path;
    }

    private static String sanitizeId(String id) {
        return id.replaceAll("[^a-zA-Z0-9._-]", "_");
    }

    private static boolean isZeroBlock(byte[] block) {
        for (byte b : block) {
            if (b != 0) return false;
        }
        return true;
    }

    private static String readTarString(byte[] buf, int off, int len) {
        int end = off;
        int max = off + len;
        while (end < max && buf[end] != 0) end++;
        return new String(buf, off, end - off, StandardCharsets.UTF_8).trim();
    }

    private static long parseOctalOrBase256(byte[] buf, int off, int len) {
        if ((buf[off] & 0x80) != 0) {
            long val = buf[off] & 0x7F;
            for (int i = 1; i < len; i++) {
                val = (val << 8) | (buf[off + i] & 0xFF);
            }
            return val;
        }
        long val = 0;
        for (int i = off; i < off + len; i++) {
            byte b = buf[i];
            if (b == 0 || b == ' ') continue;
            if (b >= '0' && b <= '7') {
                val = (val << 3) | (b - '0');
            }
        }
        return val;
    }

    private static void skipTarPadding(InputStream in, long size) throws IOException {
        long rem = size % 512;
        if (rem != 0) {
            skipFully(in, 512 - rem);
        }
    }

    private static int readFullyOrLess(InputStream in, byte[] buf, int len) throws IOException {
        int total = 0;
        while (total < len) {
            int n = in.read(buf, total, len - total);
            if (n < 0) break;
            total += n;
        }
        return total;
    }

    private static void readFully(InputStream in, byte[] buf, int len) throws IOException {
        int r = readFullyOrLess(in, buf, len);
        if (r < len) throw new EOFException("Unexpected EOF");
    }

    private static void skipFully(InputStream in, long bytes) throws IOException {
        byte[] tmp = new byte[4096];
        long rem = bytes;
        while (rem > 0) {
            long skipped = in.skip(rem);
            if (skipped > 0) {
                rem -= skipped;
            } else {
                int r = in.read(tmp, 0, (int) Math.min(tmp.length, rem));
                if (r < 0) throw new EOFException("Unexpected EOF while skipping");
                rem -= r;
            }
        }
    }

    private static byte[] readFileBytes(File f) throws IOException {
        try (FileInputStream fis = new FileInputStream(f);
             ByteArrayOutputStream bos = new ByteArrayOutputStream()) {
            byte[] buf = new byte[8192];
            int r;
            while ((r = fis.read(buf)) != -1) bos.write(buf, 0, r);
            return bos.toByteArray();
        }
    }

    private static void copyRecursive(File src, File dst) throws IOException {
        if (src.isDirectory()) {
            if (!dst.exists()) dst.mkdirs();
            File[] children = src.listFiles();
            if (children != null) {
                for (File c : children) {
                    copyRecursive(c, new File(dst, c.getName()));
                }
            }
        } else {
            try (FileInputStream in = new FileInputStream(src);
                 FileOutputStream out = new FileOutputStream(dst)) {
                byte[] buf = new byte[32768];
                int r;
                while ((r = in.read(buf)) != -1) out.write(buf, 0, r);
            }
            dst.setExecutable(true, false);
        }
    }

    private static void deleteRecursive(File f) {
        if (f.isDirectory()) {
            File[] children = f.listFiles();
            if (children != null) {
                for (File c : children) deleteRecursive(c);
            }
        }
        f.delete();
    }

    private static class BoundedInputStream extends InputStream {
        private final InputStream delegate;
        private long remaining;

        BoundedInputStream(InputStream delegate, long limit) {
            this.delegate = delegate;
            this.remaining = limit;
        }

        @Override
        public int read() throws IOException {
            if (remaining <= 0) return -1;
            int b = delegate.read();
            if (b >= 0) remaining--;
            return b;
        }

        @Override
        public int read(byte[] b, int off, int len) throws IOException {
            if (remaining <= 0) return -1;
            int toRead = (int) Math.min(len, remaining);
            int n = delegate.read(b, off, toRead);
            if (n > 0) remaining -= n;
            return n;
        }
    }
}
