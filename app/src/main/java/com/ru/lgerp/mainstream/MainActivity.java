package com.ru.lgerp.mainstream;

import android.Manifest;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.graphics.ImageFormat;
import android.graphics.SurfaceTexture;
import android.hardware.Camera;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.media.AudioAttributes;
import android.media.MediaPlayer;
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

public class MainActivity extends SDLActivity implements SensorEventListener {
    private static final String TAG = "Mainstream";

    public static final String EXTRA_GAME_DIR = "game_dir";
    public static final String EXTRA_GAME_MAIN = "game_main";
    public static final String EXTRA_GAME_WIDTH = "game_width";
    public static final String EXTRA_GAME_HEIGHT = "game_height";

    public static native void nativeSetGameConfig(String gameDir, String gameMain, int width, int height);
    public static native void nativeUpdateSensors(float gx, float gy, float gz, float ax, float ay, float az, float q0, float q1, float q2, float q3);
    public static native void nativeUpdateCameraFrame(byte[] nv21, int width, int height);

    private static Camera sCamera;
    private static SurfaceTexture sCamTexture;
    private static android.os.HandlerThread sCamThread;
    private static int sCamW = 640;
    private static int sCamH = 480;
    private static MediaPlayer ndlMediaPlayer;
    private static String currentGameDir = "";

    private SensorManager sensorManager;
    private final float[] gyroVals = new float[3];
    private final float[] accelVals = new float[3];
    private final float[] quatVals = new float[] { 1.0f, 0.0f, 0.0f, 0.0f };

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
        String gameDir = getIntent().getStringExtra(EXTRA_GAME_DIR);
        String gameMain = getIntent().getStringExtra(EXTRA_GAME_MAIN);
        int gameW = getIntent().getIntExtra(EXTRA_GAME_WIDTH, 1920);
        int gameH = getIntent().getIntExtra(EXTRA_GAME_HEIGHT, 1080);

        if (gameDir == null || gameDir.isEmpty()) {
            // Fallback to internal files directory if launched directly
            extractAssetsIfNeeded();
            gameDir = getFilesDir().getAbsolutePath();
        }
        if (gameMain == null || gameMain.isEmpty()) {
            gameMain = "mainstream";
        }

        currentGameDir = gameDir;
        super.onCreate(savedInstanceState);
        if (!mBrokenLibraries) {
            nativeSetGameConfig(gameDir, gameMain, gameW, gameH);
        }

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

        sensorManager = (SensorManager) getSystemService(SENSOR_SERVICE);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (sensorManager != null) {
            Sensor gyro = sensorManager.getDefaultSensor(Sensor.TYPE_GYROSCOPE);
            Sensor accel = sensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
            Sensor rot = sensorManager.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR);
            if (gyro != null) sensorManager.registerListener(this, gyro, SensorManager.SENSOR_DELAY_GAME);
            if (accel != null) sensorManager.registerListener(this, accel, SensorManager.SENSOR_DELAY_GAME);
            if (rot != null) sensorManager.registerListener(this, rot, SensorManager.SENSOR_DELAY_GAME);
        }
    }

    @Override
    protected void onPause() {
        if (sensorManager != null) {
            sensorManager.unregisterListener(this);
        }
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        stopV4L2Camera();
        ndlMediaUnload();
        super.onDestroy();
        // Exit :game process cleanly so fixed ELF mappings can reload another game cleanly
        System.exit(0);
    }

    public static int ndlMediaLoad(String rawPath) {
        if (rawPath == null || rawPath.isEmpty()) return -1;
        String p = rawPath;
        if (p.startsWith("file://")) p = p.substring(7);
        File f = null;
        if (!currentGameDir.isEmpty() && !p.startsWith(currentGameDir)) {
            String rel = p;
            int idx = rel.indexOf("assets/");
            if (idx >= 0) rel = rel.substring(idx);
            else {
                idx = rel.indexOf("resources/");
                if (idx >= 0) rel = rel.substring(idx);
                else {
                    idx = rel.indexOf("snd/");
                    if (idx >= 0) rel = rel.substring(idx);
                    else while (rel.startsWith("/")) rel = rel.substring(1);
                }
            }
            f = new File(currentGameDir, rel);
            if (!f.exists() && !rel.startsWith("assets/")) {
                File fAssets = new File(currentGameDir, "assets/" + rel);
                if (fAssets.exists()) f = fAssets;
            }
        }
        if (f == null || !f.exists()) {
            File direct = new File(p);
            if (direct.exists()) f = direct;
        }
        if (f == null || !f.exists()) {
            Log.w(TAG, "ndlMediaLoad: file not found: " + rawPath + " (gameDir=" + currentGameDir + ", tried=" + (f != null ? f.getAbsolutePath() : "null") + ")");
            return 0;
        }
        try {
            ndlMediaUnload();
            MediaPlayer mp = new MediaPlayer();
            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.LOLLIPOP) {
                mp.setAudioAttributes(new AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_GAME)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                        .build());
            }
            java.io.FileInputStream fis = new java.io.FileInputStream(f);
            try {
                mp.setDataSource(fis.getFD());
            } finally {
                fis.close();
            }
            mp.setLooping(true);
            mp.setVolume(1.0f, 1.0f);
            mp.prepare();
            mp.start();
            ndlMediaPlayer = mp;
            Log.i(TAG, "ndlMediaLoad playing: " + f.getAbsolutePath());
            return 0;
        } catch (Exception e) {
            Log.e(TAG, "ndlMediaLoad error for " + rawPath + " -> " + f.getAbsolutePath(), e);
            return 0;
        }
    }

    public static void ndlMediaPlay() {
        try {
            if (ndlMediaPlayer != null && !ndlMediaPlayer.isPlaying()) ndlMediaPlayer.start();
        } catch (Exception ignored) {}
    }

    public static void ndlMediaPause() {
        try {
            if (ndlMediaPlayer != null && ndlMediaPlayer.isPlaying()) ndlMediaPlayer.pause();
        } catch (Exception ignored) {}
    }

    public static void ndlMediaSeekTo(int ms) {
        try {
            if (ndlMediaPlayer != null) ndlMediaPlayer.seekTo(ms);
        } catch (Exception ignored) {}
    }

    public static void ndlMediaUnload() {
        try {
            if (ndlMediaPlayer != null) {
                ndlMediaPlayer.stop();
                ndlMediaPlayer.release();
                ndlMediaPlayer = null;
            }
        } catch (Exception ignored) {}
    }

    public static synchronized int startV4L2Camera(final int reqW, final int reqH) {
        if (sCamera != null) return 0;
        final int[] result = new int[] { -1 };
        final Object lock = new Object();
        if (sCamThread == null) {
            sCamThread = new android.os.HandlerThread("V4L2Camera");
            sCamThread.start();
        }
        android.os.Handler handler = new android.os.Handler(sCamThread.getLooper());
        handler.post(new Runnable() {
            @Override
            public void run() {
                try {
                    int camId = -1;
                    Camera.CameraInfo info = new Camera.CameraInfo();
                    int count = Camera.getNumberOfCameras();
                    for (int i = 0; i < count; i++) {
                        Camera.getCameraInfo(i, info);
                        if (info.facing == Camera.CameraInfo.CAMERA_FACING_FRONT) {
                            camId = i;
                            break;
                        }
                    }
                    if (camId < 0 && count > 0) camId = 0;
                    if (camId >= 0) {
                        Camera cam = Camera.open(camId);
                        Camera.Parameters params = cam.getParameters();
                        Camera.Size best = null;
                        int bestDiff = Integer.MAX_VALUE;
                        for (Camera.Size sz : params.getSupportedPreviewSizes()) {
                            int diff = Math.abs(sz.width - reqW) + Math.abs(sz.height - reqH);
                            if (diff < bestDiff) {
                                bestDiff = diff;
                                best = sz;
                            }
                        }
                        if (best != null) {
                            sCamW = best.width;
                            sCamH = best.height;
                            params.setPreviewSize(sCamW, sCamH);
                        }
                        params.setPreviewFormat(ImageFormat.NV21);
                        cam.setParameters(params);

                        sCamTexture = new SurfaceTexture(0);
                        cam.setPreviewTexture(sCamTexture);

                        int bufSize = sCamW * sCamH * ImageFormat.getBitsPerPixel(ImageFormat.NV21) / 8;
                        cam.addCallbackBuffer(new byte[bufSize]);
                        cam.addCallbackBuffer(new byte[bufSize]);
                        cam.setPreviewCallbackWithBuffer(new Camera.PreviewCallback() {
                            @Override
                            public void onPreviewFrame(byte[] data, Camera camera) {
                                if (data != null) {
                                    nativeUpdateCameraFrame(data, sCamW, sCamH);
                                    camera.addCallbackBuffer(data);
                                }
                            }
                        });
                        cam.startPreview();
                        sCamera = cam;
                        result[0] = 0;
                        Log.i(TAG, "Started Android Camera " + camId + " (" + sCamW + "x" + sCamH + ")");
                    }
                } catch (Exception e) {
                    Log.e(TAG, "startV4L2Camera failed", e);
                }
                synchronized (lock) {
                    lock.notifyAll();
                }
            }
        });
        synchronized (lock) {
            try {
                lock.wait(3000);
            } catch (InterruptedException ignored) {}
        }
        return result[0];
    }

    public static synchronized void stopV4L2Camera() {
        try {
            if (sCamera != null) {
                sCamera.setPreviewCallbackWithBuffer(null);
                sCamera.stopPreview();
                sCamera.release();
                sCamera = null;
            }
            if (sCamTexture != null) {
                sCamTexture.release();
                sCamTexture = null;
            }
        } catch (Exception ignored) {}
    }

    @Override
    public void onSensorChanged(SensorEvent event) {
        if (event.sensor.getType() == Sensor.TYPE_GYROSCOPE) {
            gyroVals[0] = event.values[0];
            gyroVals[1] = event.values[1];
            gyroVals[2] = event.values[2];
        } else if (event.sensor.getType() == Sensor.TYPE_ACCELEROMETER) {
            accelVals[0] = event.values[0];
            accelVals[1] = event.values[1];
            accelVals[2] = event.values[2];
        } else if (event.sensor.getType() == Sensor.TYPE_ROTATION_VECTOR) {
            SensorManager.getQuaternionFromVector(quatVals, event.values);
        }
        nativeUpdateSensors(
                gyroVals[0], gyroVals[1], gyroVals[2],
                accelVals[0], accelVals[1], accelVals[2],
                quatVals[0], quatVals[1], quatVals[2], quatVals[3]);
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {}

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        int keyCode = event.getKeyCode();
        // Let Android handle system volume / power keys normally
        if (keyCode == KeyEvent.KEYCODE_VOLUME_UP ||
            keyCode == KeyEvent.KEYCODE_VOLUME_DOWN ||
            keyCode == KeyEvent.KEYCODE_VOLUME_MUTE ||
            keyCode == KeyEvent.KEYCODE_POWER) {
            return super.dispatchKeyEvent(event);
        }

        // Map gamepad A/B to Enter/Back so controllers & TV remotes work uniformly
        int mappedCode = keyCode;
        if (keyCode == KeyEvent.KEYCODE_BUTTON_A) {
            mappedCode = KeyEvent.KEYCODE_ENTER;
        } else if (keyCode == KeyEvent.KEYCODE_BUTTON_B) {
            mappedCode = KeyEvent.KEYCODE_BACK;
        }

        if (event.getAction() == KeyEvent.ACTION_DOWN) {
            if (event.getRepeatCount() == 0) {
                SDLActivity.onNativeKeyDown(mappedCode);
            }
            return true;
        } else if (event.getAction() == KeyEvent.ACTION_UP) {
            SDLActivity.onNativeKeyUp(mappedCode);
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
