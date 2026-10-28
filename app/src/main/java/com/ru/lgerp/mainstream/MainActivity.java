package com.ru.lgerp.mainstream;

import android.content.res.AssetManager;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

public class MainActivity extends SDLActivity {
    private static final String TAG = "Mainstream";

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "gnustl_shared",
            "SDL2",
            "SDL2_mixer",
            "mainstream_loader"
        };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // Extract assets to internal files directory BEFORE SDL initializes or starts threads
        extractAssetsIfNeeded();

        super.onCreate(savedInstanceState);

        // Keep screen on while playing
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        // Immersive sticky mode
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    private void extractAssetsIfNeeded() {
        File filesDir = getFilesDir();
        File marker = new File(filesDir, ".assets_extracted_v2");
        if (!marker.exists()) {
            Log.i(TAG, "Extracting assets to " + filesDir.getAbsolutePath() + "...");
            copyAsset(getAssets(), "", filesDir);
            try {
                marker.createNewFile();
            } catch (IOException e) {
                Log.e(TAG, "Failed to create marker", e);
            }
            Log.i(TAG, "Assets extraction completed.");
        }
    }

    private void copyAsset(AssetManager assetManager, String path, File destDir) {
        if (!path.isEmpty()) {
            // Try opening as file first
            try (InputStream in = assetManager.open(path)) {
                File destFile = new File(destDir, path);
                if (destFile.isDirectory()) {
                    deleteRecursive(destFile);
                }
                File parent = destFile.getParentFile();
                if (parent != null) parent.mkdirs();
                try (OutputStream out = new FileOutputStream(destFile)) {
                    byte[] buffer = new byte[8192];
                    int read;
                    while ((read = in.read(buffer)) != -1) {
                        out.write(buffer, 0, read);
                    }
                    out.flush();
                }
                destFile.setExecutable(true, false);
                return;
            } catch (IOException notAFile) {
                // Not a file, proceed to list as directory
            }
        }

        // It is a directory, list contents
        try {
            String[] files = assetManager.list(path);
            if (files != null) {
                for (String file : files) {
                    if (path.isEmpty() && (file.equals("images") || file.equals("sounds") || file.equals("webkit"))) {
                        continue;
                    }
                    String subPath = path.isEmpty() ? file : path + "/" + file;
                    copyAsset(assetManager, subPath, destDir);
                }
            }
        } catch (IOException e) {
            Log.e(TAG, "Failed to list asset directory: " + path, e);
        }
    }

    private void deleteRecursive(File fileOrDirectory) {
        if (fileOrDirectory.isDirectory()) {
            File[] children = fileOrDirectory.listFiles();
            if (children != null) {
                for (File child : children) {
                    deleteRecursive(child);
                }
            }
        }
        fileOrDirectory.delete();
    }
}
