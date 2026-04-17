package com.p2995080.p133850;

import android.app.Activity;
import android.content.res.Resources;
import android.graphics.Color;
import android.os.Bundle;
import android.util.DisplayMetrics;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;

public class MainActivity extends Activity {

    private WebView myWebView;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        requestWindowFeature(Window.FEATURE_NO_TITLE);
        
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);

        getWindow().setFlags(
                WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED,
                WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED);

        getWindow().setFlags(
                WindowManager.LayoutParams.FLAG_FULLSCREEN,
                WindowManager.LayoutParams.FLAG_FULLSCREEN);

        DisplayMetrics metrics = Resources.getSystem().getDisplayMetrics();
        int screenWidth = metrics.widthPixels;
        int screenHeight = metrics.heightPixels;

        int targetWidth = screenWidth;
        int targetHeight = (int) (screenWidth * 9.0f / 16.0f);

        if (targetHeight > screenHeight) {
            targetHeight = screenHeight;
            targetWidth = (int) (screenHeight * 16.0f / 9.0f);
        }

        myWebView = new WebView(this);
        myWebView.setBackgroundColor(Color.BLACK);

        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(targetWidth, targetHeight);
        params.gravity = android.view.Gravity.CENTER;
        setContentView(myWebView, params);

        WebSettings settings = myWebView.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setAllowFileAccess(true);
        settings.setDomStorageEnabled(true);
        settings.setUseWideViewPort(true);
        settings.setLoadWithOverviewMode(true);
        settings.setMediaPlaybackRequiresUserGesture(false);
        settings.setSupportZoom(false);
        settings.setBuiltInZoomControls(false);
        settings.setDisplayZoomControls(false);

        myWebView.setWebViewClient(new WebViewClient());
        myWebView.loadUrl("file:///android_asset/index.html");
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (myWebView != null) {
            myWebView.onPause();
            myWebView.pauseTimers();
            myWebView.loadUrl("javascript:if(window.gamePause){gamePause();}else{document.querySelectorAll('audio,video').forEach(m=>m.pause());}");
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (myWebView != null) {
            myWebView.onResume();
            myWebView.resumeTimers();
            myWebView.loadUrl("javascript:document.querySelectorAll('audio,video').forEach(m=>m.play()); if(window.audioContext){window.audioContext.resume();}");
        }
    }
}
