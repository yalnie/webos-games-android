package com.ru.lgerp.mainstream;

import android.content.res.AssetManager;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.view.WindowManager;
import android.view.KeyEvent;
import android.util.DisplayMetrics;
import android.widget.RelativeLayout;

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

        if (android.os.Build.VERSION.SDK_INT >= 33) {
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT,
                () -> {
                    SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_BACK);
                    SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_BACK);
                }
            );
        }
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK) {
            if (event.getAction() == KeyEvent.ACTION_DOWN) {
                SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_BACK);
            } else if (event.getAction() == KeyEvent.ACTION_UP) {
                SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_BACK);
            }
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    public void onBackPressed() {
        SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_BACK);
        SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_BACK);
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
