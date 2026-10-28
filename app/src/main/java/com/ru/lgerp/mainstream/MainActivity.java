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
            "stdc++",
            "SDL2",
            "SDL2_mixer",
            "mainstream_loader"
        };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
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

        // Extract assets to internal files directory if not already done
        extractAssetsIfNeeded();
    }

    private void extractAssetsIfNeeded() {
        File filesDir = getFilesDir();
        File marker = new File(filesDir, ".assets_extracted_v1");
        if (!marker.exists()) {
            Log.i(TAG, "Extracting assets to " + filesDir.getAbsolutePath() + "...");
            copyAssetFolder(getAssets(), "", filesDir.getAbsolutePath());
            try {
                marker.createNewFile();
            } catch (IOException e) {
                Log.e(TAG, "Failed to create marker", e);
            }
            Log.i(TAG, "Assets extraction completed.");
        }
    }

    private boolean copyAssetFolder(AssetManager assetManager, String fromAssetPath, String toPath) {
        try {
            String[] files = assetManager.list(fromAssetPath);
            new File(toPath).mkdirs();
            boolean res = true;
            if (files != null) {
                for (String file : files) {
                    if (file.equals("images") || file.equals("sounds") || file.equals("webkit")) {
                        continue;
                    }
                    String subAsset = fromAssetPath.isEmpty() ? file : fromAssetPath + "/" + file;
                    String subDest = toPath + "/" + file;
                    String[] subFiles = assetManager.list(subAsset);
                    if (subFiles != null && subFiles.length > 0) {
                        res &= copyAssetFolder(assetManager, subAsset, subDest);
                    } else {
                        res &= copyAssetFile(assetManager, subAsset, subDest);
                    }
                }
            }
            return res;
        } catch (Exception e) {
            Log.e(TAG, "copyAssetFolder error on: " + fromAssetPath, e);
            return false;
        }
    }

    private boolean copyAssetFile(AssetManager assetManager, String fromAssetPath, String toPath) {
        File destFile = new File(toPath);
        if (destFile.exists()) return true;
        try (InputStream in = assetManager.open(fromAssetPath);
             OutputStream out = new FileOutputStream(destFile)) {
            byte[] buffer = new byte[8192];
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }
            out.flush();
            destFile.setExecutable(true, false);
            return true;
        } catch (Exception e) {
            Log.e(TAG, "copyAssetFile failed: " + fromAssetPath, e);
            return false;
        }
    }
}
