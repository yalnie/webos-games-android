package com.ru.lgerp.mainstream;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.app.ProgressDialog;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.BaseAdapter;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

public class LauncherActivity extends Activity {
    private static final int REQ_PICK_IPK = 1001;
    private static final int REQ_CAMERA_PERM = 1002;
    private static final int MENU_INSTALL_ID = 1;

    private IpkInstaller.GameEntry pendingCameraGame = null;

    private List<IpkInstaller.GameEntry> games = new ArrayList<>();
    private GameAdapter adapter;
    private ListView listView;
    private LinearLayout emptyContainer;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setFitsSystemWindows(true);
        root.setLayoutParams(new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        listView = new ListView(this);
        listView.setLayoutParams(new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1.0f));

        emptyContainer = new LinearLayout(this);
        emptyContainer.setOrientation(LinearLayout.VERTICAL);
        emptyContainer.setGravity(Gravity.CENTER);
        emptyContainer.setPadding(48, 48, 48, 48);
        emptyContainer.setLayoutParams(new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1.0f));

        TextView emptyText = new TextView(this);
        emptyText.setText("No webOS games installed yet.\nSelect an .ipk file to install and play.");
        emptyText.setGravity(Gravity.CENTER);
        emptyText.setTextSize(16f);
        emptyText.setPadding(0, 0, 0, 32);

        Button installBtn = new Button(this);
        installBtn.setText("Install .ipk File");
        installBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                openIpkPicker();
            }
        });

        emptyContainer.addView(emptyText);
        emptyContainer.addView(installBtn);

        root.addView(listView);
        root.addView(emptyContainer);
        setContentView(root);

        adapter = new GameAdapter();
        listView.setAdapter(adapter);
        listView.setOnItemClickListener(new AdapterView.OnItemClickListener() {
            @Override
            public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
                launchGame(games.get(position));
            }
        });
        listView.setOnItemLongClickListener(new AdapterView.OnItemLongClickListener() {
            @Override
            public boolean onItemLongClick(AdapterView<?> parent, View view, int position, long id) {
                confirmUninstall(games.get(position));
                return true;
            }
        });

        refreshGames();
        handleIncomingIntent(getIntent());
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        handleIncomingIntent(intent);
    }

    private void handleIncomingIntent(Intent intent) {
        if (intent != null && Intent.ACTION_VIEW.equals(intent.getAction()) && intent.getData() != null) {
            installIpk(intent.getData());
        }
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        MenuItem item = menu.add(0, MENU_INSTALL_ID, 0, "Install IPK");
        item.setShowAsAction(MenuItem.SHOW_AS_ACTION_ALWAYS);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        if (item.getItemId() == MENU_INSTALL_ID) {
            openIpkPicker();
            return true;
        }
        return super.onOptionsItemSelected(item);
    }

    private void openIpkPicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, REQ_PICK_IPK);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQ_PICK_IPK && resultCode == RESULT_OK && data != null && data.getData() != null) {
            installIpk(data.getData());
        }
    }

    private void installIpk(final Uri uri) {
        final ProgressDialog progress = new ProgressDialog(this);
        progress.setMessage("Installing IPK...");
        progress.setCancelable(false);
        progress.show();

        new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    final IpkInstaller.GameEntry installed = IpkInstaller.installFromUri(LauncherActivity.this, uri);
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            progress.dismiss();
                            refreshGames();
                            Toast.makeText(LauncherActivity.this, "Installed " + installed.title, Toast.LENGTH_SHORT).show();
                        }
                    });
                } catch (final Exception e) {
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            progress.dismiss();
                            new AlertDialog.Builder(LauncherActivity.this)
                                    .setTitle("Install Failed")
                                    .setMessage(e.getMessage())
                                    .setPositiveButton("OK", null)
                                    .show();
                        }
                    });
                }
            }
        }).start();
    }

    private void refreshGames() {
        games = IpkInstaller.listInstalledGames(this);
        adapter.notifyDataSetChanged();
        boolean isEmpty = games.isEmpty();
        listView.setVisibility(isEmpty ? View.GONE : View.VISIBLE);
        emptyContainer.setVisibility(isEmpty ? View.VISIBLE : View.GONE);
    }

    private static boolean requiresCamera(IpkInstaller.GameEntry game) {
        File bin = new File(game.dir, game.main);
        if (!bin.exists() || !bin.isFile()) return false;
        byte[] needle = "/dev/video".getBytes();
        byte[] buf = new byte[65536];
        int match = 0;
        try (java.io.FileInputStream fis = new java.io.FileInputStream(bin)) {
            int n;
            while ((n = fis.read(buf)) > 0) {
                for (int i = 0; i < n; i++) {
                    if (buf[i] == needle[match]) {
                        match++;
                        if (match == needle.length) return true;
                    } else {
                        match = (buf[i] == needle[0]) ? 1 : 0;
                    }
                }
            }
        } catch (Exception ignored) {}
        return false;
    }

    private void launchGame(IpkInstaller.GameEntry game) {
        if (android.os.Build.VERSION.SDK_INT >= 23 && requiresCamera(game)) {
            if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
                pendingCameraGame = game;
                requestPermissions(new String[] { Manifest.permission.CAMERA }, REQ_CAMERA_PERM);
                return;
            }
        }
        startGameActivity(game);
    }

    private void startGameActivity(IpkInstaller.GameEntry game) {
        Intent intent = new Intent(this, MainActivity.class);
        intent.putExtra(MainActivity.EXTRA_GAME_DIR, game.dir.getAbsolutePath());
        intent.putExtra(MainActivity.EXTRA_GAME_MAIN, game.main);
        intent.putExtra(MainActivity.EXTRA_GAME_WIDTH, game.width);
        intent.putExtra(MainActivity.EXTRA_GAME_HEIGHT, game.height);
        startActivity(intent);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQ_CAMERA_PERM && pendingCameraGame != null) {
            IpkInstaller.GameEntry game = pendingCameraGame;
            pendingCameraGame = null;
            startGameActivity(game);
        }
    }

    private void confirmUninstall(final IpkInstaller.GameEntry game) {
        new AlertDialog.Builder(this)
                .setTitle("Uninstall " + game.title + "?")
                .setMessage("This will remove the game files and save data for " + game.id + ".")
                .setPositiveButton("Uninstall", new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialog, int which) {
                        IpkInstaller.uninstallGame(LauncherActivity.this, game.id);
                        refreshGames();
                    }
                })
                .setNegativeButton("Cancel", null)
                .show();
    }

    private class GameAdapter extends BaseAdapter {
        @Override
        public int getCount() {
            return games.size();
        }

        @Override
        public Object getItem(int position) {
            return games.get(position);
        }

        @Override
        public long getItemId(int position) {
            return position;
        }

        @Override
        public View getView(int position, View convertView, ViewGroup parent) {
            IpkInstaller.GameEntry game = games.get(position);
            LinearLayout row;
            ImageView iconView;
            TextView titleView;
            TextView subView;

            if (convertView instanceof LinearLayout) {
                row = (LinearLayout) convertView;
                iconView = (ImageView) row.getChildAt(0);
                LinearLayout textCol = (LinearLayout) row.getChildAt(1);
                titleView = (TextView) textCol.getChildAt(0);
                subView = (TextView) textCol.getChildAt(1);
            } else {
                row = new LinearLayout(LauncherActivity.this);
                row.setOrientation(LinearLayout.HORIZONTAL);
                row.setGravity(Gravity.CENTER_VERTICAL);
                row.setPadding(32, 24, 32, 24);

                iconView = new ImageView(LauncherActivity.this);
                LinearLayout.LayoutParams iconLp = new LinearLayout.LayoutParams(128, 128);
                iconLp.rightMargin = 32;
                iconView.setLayoutParams(iconLp);
                iconView.setScaleType(ImageView.ScaleType.FIT_CENTER);

                LinearLayout textCol = new LinearLayout(LauncherActivity.this);
                textCol.setOrientation(LinearLayout.VERTICAL);
                textCol.setLayoutParams(new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f));

                titleView = new TextView(LauncherActivity.this);
                titleView.setTextSize(18f);

                subView = new TextView(LauncherActivity.this);
                subView.setTextSize(13f);
                subView.setAlpha(0.75f);

                textCol.addView(titleView);
                textCol.addView(subView);
                row.addView(iconView);
                row.addView(textCol);
            }

            titleView.setText(game.title);
            subView.setText(game.id + "  •  " + game.width + "x" + game.height);

            Bitmap bmp = null;
            if (!game.icon.isEmpty()) {
                File iconFile = new File(game.dir, game.icon);
                if (iconFile.exists()) {
                    bmp = BitmapFactory.decodeFile(iconFile.getAbsolutePath());
                }
            }
            if (bmp != null) {
                iconView.setImageBitmap(bmp);
            } else {
                iconView.setImageResource(R.mipmap.ic_mainstream_launcher);
            }

            return row;
        }
    }
}
