package io.github.seaofstarproject.tod;

import android.content.Context;
import android.os.Bundle;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Where the game's files live and what the runtime (wiirecomp_main) is told about them.
 *
 * External app storage (.../Android/data/&lt;package&gt;/files/SO3EE9/, reachable with adb push, never executable):
 *   SO3EE9.wrd      the packed image (carries the android-arm64 title library), or
 *   tree/           an extracted disc (main.dol, boot.bin, fst.bin, here or in tree/sys), which needs libtitle.so in the APK
 *   saves/          WII_RECOMP_SAVES
 *   title-data/     WII_RECOMP_TITLE_DATA
 *   logs/run.log    WII_RECOMP_LOG_FILE (previous run: run.prev.log)
 * Internal storage (getFilesDir(), executable, private): native-cache (the title library the image
 * carries is written here and dlopen'ed), shader-cache, control (the FIFO), controllers.ini and friends.
 */
final class GamePaths {
    static final String GAME_ID = "SO3EE9";
    static final String ENV_PREFIX = "WII_RECOMP_";

    final File filesDir;      // internal
    final File dataDir;       // external (or internal when there is no external storage)
    final File image;
    final File tree;
    final File logFile;
    final File libDir;        // nativeLibraryDir
    final boolean external;
    private final Map<String, String> env = new LinkedHashMap<>();

    GamePaths(Context context) {
        filesDir = context.getFilesDir();
        File ext = context.getExternalFilesDir(null);
        external = ext != null;
        dataDir = new File(external ? ext : filesDir, GAME_ID);
        image = new File(dataDir, GAME_ID + ".wrd");
        tree = new File(dataDir, "tree");
        logFile = new File(new File(dataDir, "logs"), "run.log");
        libDir = new File(context.getApplicationInfo().nativeLibraryDir);

        env.put("WII_RECOMP_SAVES", new File(dataDir, "saves").getPath());
        env.put("WII_RECOMP_TITLE_DATA", new File(dataDir, "title-data").getPath());
        env.put("WII_RECOMP_LOG_FILE", logFile.getPath());
        env.put("WII_RECOMP_SHADER_CACHE", new File(filesDir, "shader-cache").getPath());
        env.put("WII_RECOMP_CACHE", new File(filesDir, "native-cache").getPath());
        env.put("WII_RECOMP_CONTROL", new File(filesDir, "control").getPath());
        env.put("WII_RECOMP_GAPS", new File(filesDir, "gaps.txt").getPath());
        env.put("WII_RECOMP_REMOTE_SLOTS", new File(filesDir, "remotes.txt").getPath());
        env.put("WII_RECOMP_CONTROLLERS", new File(filesDir, "controllers.ini").getPath());
        env.put("WII_RECOMP_CERTIFICATES", new File(filesDir, "certificates").getPath());
    }

    File libSdl() { return new File(libDir, "libSDL3.so"); }
    File libRuntime() { return new File(libDir, "libwiirecomp.so"); }
    File libTitle() { return new File(libDir, "libtitle.so"); }

    boolean hasImage() { return image.isFile(); }

    /** The folder holding main.dol, or null. */
    File treeSys() {
        if (new File(tree, "main.dol").isFile()) return tree;
        File sys = new File(tree, "sys");
        return new File(sys, "main.dol").isFile() ? sys : null;
    }

    void ensureDirs() {
        for (File dir : new File[] {dataDir, new File(dataDir, "saves"), new File(dataDir, "title-data"),
                logFile.getParentFile(), new File(filesDir, "shader-cache"), new File(filesDir, "native-cache")}) {
            dir.mkdirs();
        }
    }

    /** Keeps one earlier log: the runtime appends to run.log, so each launch starts a new one. */
    void rotateLog() {
        if (logFile.isFile()) {
            File previous = new File(logFile.getParentFile(), "run.prev.log");
            previous.delete();
            logFile.renameTo(previous);
        }
    }

    /**
     * The argument the runtime boots: an "image" extra, else the image, else the extracted tree.
     * Returns null when there is nothing to boot. Boot from a tree also needs WII_RECOMP_NATIVE.
     */
    String bootTarget(Bundle extras) {
        String given = extras == null ? null : extras.getString("image");
        if (given != null && !given.isEmpty()) return given;
        if (hasImage()) return image.getPath();
        File sys = treeSys();
        return sys == null ? null : sys.getPath();
    }

    /** The WII_RECOMP_ variables to set before the runtime starts; WII_RECOMP_ extras of the intent win. */
    Map<String, String> environment(Bundle extras, String bootTarget) {
        Map<String, String> result = new LinkedHashMap<>(env);
        if (bootTarget != null && new File(bootTarget).isDirectory() && libTitle().isFile()) {
            result.put("WII_RECOMP_NATIVE", libTitle().getPath());
        }
        if (extras != null) {
            for (String key : extras.keySet()) {
                Object value = extras.get(key);
                if (key.startsWith(ENV_PREFIX) && value != null) result.put(key, String.valueOf(value));
            }
        }
        return result;
    }

    /** The last lines of the log (previous run's when this run has not written yet). */
    String tailLog(int lines) {
        File file = logFile.isFile() ? logFile : new File(logFile.getParentFile(), "run.prev.log");
        if (!file.isFile()) return "(no log yet: " + logFile.getPath() + ")";
        try (RandomAccessFile in = new RandomAccessFile(file, "r")) {
            long size = in.length();
            int take = (int) Math.min(size, 32 * 1024);
            byte[] bytes = new byte[take];
            in.seek(size - take);
            in.readFully(bytes);
            String text = new String(bytes, StandardCharsets.UTF_8);
            String[] all = text.split("\\r?\\n");
            StringBuilder out = new StringBuilder(file.getName()).append(" (").append(size).append(" bytes)\n");
            for (int i = Math.max(0, all.length - lines); i < all.length; i++) out.append(all[i]).append('\n');
            return out.toString();
        } catch (IOException e) {
            return "can't read " + file + ": " + e;
        }
    }

    /** Appends a line to the log, for what only Java knows (what it launched). */
    void logLine(String line) {
        try (FileOutputStream out = new FileOutputStream(logFile, true)) {
            out.write((line + "\n").getBytes(StandardCharsets.UTF_8));
        } catch (IOException ignored) {
            // Logcat still has it.
        }
    }
}
