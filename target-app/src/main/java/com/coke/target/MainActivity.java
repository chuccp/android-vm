package com.coke.target;

import android.app.Activity;
import android.app.Dialog;
import android.content.Context;
import android.content.Intent;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.media.MediaPlayer;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.MotionEvent;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.ProgressBar;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.ToggleButton;
import android.widget.VideoView;

import java.util.concurrent.TimeUnit;

import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.Response;

public final class MainActivity extends Activity {
    private static final String HLS_URL =
            "https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8";
    private TextView statusView;
    private RuntimePanel runtimePanel;
    private Switch featureSwitch;
    private ProgressBar featureProgress;
    private TextView threadStatus;
    private TextView ioStatus;
    private TextView capabilityStatus;
    private TextView mediaStatus;
    private TextView probeView;
    private TextView dynamicBadge;
    private Handler mainHandler;
    private VideoView mediaVideo;
    private VideoView hlsVideo;
    private TextView hlsStatus;
    private TextView nettyStatus;
    private MediaPlayer audioPlayer;
    private boolean nettyServerRunning;
    private int touchCount;
    private int viewClickCount;
    private int uiMode;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        RuntimeLifecycleApp.lifecycleLog += "activity.onCreate;";
        Log.i("DexVmTargetLifecycle", "activity.onCreate");
        try {
            runDeviceDirectLine(savedInstanceState);
        } catch (Throwable vmGap) {
            // 生命周期 fixture 便携面：直跑线依赖的 framework 面（布局膨胀/媒体等）
            // VM 缺口时落便携首帧树（TextView，桥接出画输入），不终止生命周期链。
            Log.i("DexVmTargetLifecycle", "direct line gap: " + vmGap);
            TextView portable = new TextView(this);
            portable.setText("target ok\n" + RuntimeLifecycleApp.lifecycleLog);
            portable.setTextSize(16f);
            portable.setPadding(48, 48, 48, 48);
            setContentView(portable);
        }
    }

    /** 设备直跑线（UI/媒体/服务全考面；README 分发契约「设备侧直跑线」）。 */
    private void runDeviceDirectLine(Bundle savedInstanceState) {
        setContentView(R.layout.runtime_primary);
        statusView = (TextView) findViewById(R.id.runtime_status);
        featureSwitch = (Switch) findViewById(R.id.runtime_switch);
        featureProgress = (ProgressBar) findViewById(R.id.runtime_progress);
        Button actionButton = (Button) findViewById(R.id.runtime_action);
        Button hlsButton = (Button) findViewById(R.id.runtime_hls_action);
        Button nettyButton = (Button) findViewById(R.id.runtime_netty_action);
        threadStatus = (TextView) findViewById(R.id.runtime_thread_status);
        ioStatus = (TextView) findViewById(R.id.runtime_io_status);
        capabilityStatus = (TextView) findViewById(R.id.runtime_capability_status);
        mediaStatus = (TextView) findViewById(R.id.runtime_media_status);
        probeView = (TextView) findViewById(R.id.runtime_compiled_label);
        probeView.setText(
                "Compiled Binary XML orientation=" +
                getResources().getConfiguration().orientation);
        String contextResult;
        try {
            contextResult =
                    RuntimeLibraryCompatibilityFixture.contextSummary(this);
        } catch (Throwable failure) {
            contextResult = "Context=error:" + failure.getMessage();
        }
        capabilityStatus.setText(
                "Android specials: bundle=127 intent=15 " +
                "orientation=" +
                getResources().getConfiguration().orientation + " " +
                RuntimeLibraryCompatibilityFixture.packageManagerSummary(this) +
                " " + contextResult);
        wireExtendedWidgets();
        mainHandler = new Handler(Looper.getMainLooper());
        dynamicBadge = new TextView(this);
        dynamicBadge.setText("Dynamic guest badge");
        hlsVideo = (VideoView) findViewById(R.id.runtime_hls_video);
        hlsStatus = (TextView) findViewById(R.id.runtime_hls_status);
        nettyStatus = (TextView) findViewById(R.id.runtime_netty_status);
        if (hlsButton != null && hlsVideo != null && hlsStatus != null) {
            hlsButton.setOnClickListener(view -> {
                hlsStatus.setText("HLS playing " + HLS_URL);
                hlsVideo.setVisibility(View.VISIBLE);
                hlsVideo.setVideoPath(HLS_URL);
                hlsVideo.start();
            });
        }
        if (nettyButton != null && nettyStatus != null) {
            nettyButton.setOnClickListener(view ->
                    startNettyHttpServer());
        }
        runtimePanel = new RuntimePanel(this);
        initializeMedia();
        runtimePanel.updateUiCycle(
                "UI cycle 0: VISIBLE alpha=1.0 selected=false");
        mainHandler.post(() -> runtimePanel.updateAsyncStatus("Guest Handler posted UI: 0"));
        actionButton.setOnClickListener(view -> {
            viewClickCount++;
            statusView.setText("VIEW_CLICKED " + viewClickCount);
            uiMode = (uiMode + 1) % 3;
            int visible = uiMode == 1
                    ? View.INVISIBLE
                    : (uiMode == 2 ? View.GONE : View.VISIBLE);
            float alpha = uiMode == 1
                    ? 0.55f
                    : (uiMode == 2 ? 0.30f : 1.0f);
            probeView.setVisibility(visible);
            probeView.setAlpha(alpha);
            probeView.setSelected(uiMode == 1);
            probeView.setActivated(uiMode == 2);
            probeView.setRotation(uiMode == 1 ? 7.0f : 0.0f);
            probeView.setContentDescription("runtime probe mode " + uiMode);
            probeView.setTag("mode-" + uiMode);

            int badgeIndex = runtimePanel.indexOfChild(dynamicBadge);
            if (uiMode == 1 && badgeIndex < 0) {
                runtimePanel.addView(dynamicBadge);
            } else if (uiMode == 2 && badgeIndex >= 0) {
                runtimePanel.removeView(dynamicBadge);
            }

            String visibilityName = "VISIBLE";
            if (probeView.getVisibility() == View.INVISIBLE) {
                visibilityName = "INVISIBLE";
            } else if (probeView.getVisibility() == View.GONE) {
                visibilityName = "GONE";
            }
            runtimePanel.updateUiCycle(
                    "UI cycle " + uiMode
                            + ": " + visibilityName
                            + " alpha=" + probeView.getAlpha()
                            + " selected=" + probeView.isSelected()
                            + " tag=" + probeView.getTag());
        });

        runtimePanel.updateScore(TargetEntry.nativeCallFlow(18));
        addContentView(
                runtimePanel,
                new android.view.ViewGroup.LayoutParams(
                        android.view.ViewGroup.LayoutParams.MATCH_PARENT,
                        android.view.ViewGroup.LayoutParams.WRAP_CONTENT));
        new Thread(new RuntimeTask(threadStatus), "RuntimeTask-1").start();
        new Thread(new RuntimeTask(threadStatus), "RuntimeTask-2").start();
        RuntimeIoTask.createPipe();
        new Thread(new RuntimeIoTask(ioStatus, false), "RuntimeIoReader").start();
        new Thread(new RuntimeIoTask(ioStatus, true), "RuntimeIoWriter").start();
        new Thread(new RuntimeMonitorTask(runtimePanel, false), "RuntimeMonitorWaiter").start();
        new Thread(new RuntimeMonitorTask(runtimePanel, true), "RuntimeMonitorNotifier").start();
        new Thread(new RuntimeSocketTask(runtimePanel, true), "RuntimeSocketServer").start();
        new Thread(new RuntimeSocketTask(runtimePanel, false), "RuntimeSocketClient").start();
        new Thread(
                new RuntimeFileTask(runtimePanel, getFilesDir()),
                "RuntimeFileTask").start();
        new Thread(new RuntimeHttpTask(runtimePanel, true), "RuntimeHttpServer").start();
        new Thread(new RuntimeHttpTask(runtimePanel, false), "RuntimeHttpClient").start();

        Intent intent = new Intent();
        intent.setClassName("com.coke.target", "com.coke.target.RuntimeService");
        startService(intent);
    }

    /**
     * 扩展 widget 考面：CheckBox/RadioGroup+RadioButton/ToggleButton/SeekBar/
     * ImageButton（资源布局膨胀 + 点击回灌 + 状态桥接），外加 lib Dialog
     * 窗口叠加（宿主 dim 遮罩 + 多根物化）。
     */
    private void wireExtendedWidgets() {
        ListView listView = (ListView) findViewById(R.id.runtime_listview);
        if (listView != null) {
            ArrayAdapter<String> listAdapter = new ArrayAdapter<String>(this, 0,
                    new String[] {"List row 1", "List row 2", "List row 3",
                            "List row 4", "List row 5"});
            listView.setAdapter(listAdapter);
            listView.setOnClickListener(view ->
                    capabilityStatus.setText("ListView clicked"));
        }

        Button okhttpButton = (Button) findViewById(R.id.runtime_okhttp_action);
        final EditText okhttpOutput = (EditText) findViewById(R.id.runtime_okhttp_output);
        if (okhttpButton != null && okhttpOutput != null) {
            okhttpButton.setOnClickListener(view -> fetchBaiduHtml(okhttpOutput));
        }

        Spinner spinner = (Spinner) findViewById(R.id.runtime_spinner);
        if (spinner != null) {
            ArrayAdapter<String> spinnerAdapter = new ArrayAdapter<String>(this, 0,
                    new String[] {"Spinner one", "Spinner two", "Spinner three"});
            spinner.setAdapter(spinnerAdapter);
            spinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
                @Override
                public void onItemSelected(AdapterView<?> parent, View view,
                                           int position, long id) {
                    capabilityStatus.setText("Spinner selected=" + position);
                }

                @Override
                public void onNothingSelected(AdapterView<?> parent) {
                }
            });
        }

        CheckBox checkBox = (CheckBox) findViewById(R.id.runtime_checkbox);
        if (checkBox != null) {
            checkBox.setOnCheckedChangeListener((button, checked) ->
                    capabilityStatus.setText("CheckBox checked=" + checked));
        }

        final RadioGroup radioGroup =
                (RadioGroup) findViewById(R.id.runtime_radiogroup);
        if (radioGroup != null) {
            radioGroup.setOnCheckedChangeListener((group, checkedId) ->
                    capabilityStatus.setText("RadioGroup checkedId=" + checkedId));
            wireRadio(radioGroup, R.id.runtime_radio_a);
            wireRadio(radioGroup, R.id.runtime_radio_b);
            wireRadio(radioGroup, R.id.runtime_radio_c);
        }

        ToggleButton toggle = (ToggleButton) findViewById(R.id.runtime_toggle);
        if (toggle != null) {
            toggle.setTextOn("Compat ON");
            toggle.setTextOff("Compat OFF");
            toggle.setOnCheckedChangeListener((button, checked) ->
                    capabilityStatus.setText("ToggleButton checked=" + checked));
        }

        SeekBar seekBar = (SeekBar) findViewById(R.id.runtime_seekbar);
        if (seekBar != null) {
            seekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                @Override
                public void onProgressChanged(SeekBar bar, int progress, boolean fromUser) {
                    capabilityStatus.setText("SeekBar progress=" + progress);
                }

                @Override
                public void onStartTrackingTouch(SeekBar bar) {
                }

                @Override
                public void onStopTrackingTouch(SeekBar bar) {
                }
            });
        }

        ImageButton imageButton =
                (ImageButton) findViewById(R.id.runtime_imagebutton);
        if (imageButton != null) {
            imageButton.setImageResource(R.drawable.runtime_photo);
            imageButton.setOnClickListener(view ->
                    capabilityStatus.setText("ImageButton clicked"));
        }

        Button dialogButton = (Button) findViewById(R.id.runtime_dialog_action);
        if (dialogButton != null) {
            dialogButton.setOnClickListener(view -> showGuestDialog());
        }
    }

    /**
     * OkHttp 4.12 真网络面：guest 线程发 HTTPS 请求（javax.net.ssl -> 宿主
     * TlsBridge），把百度首页 HTML 源码写进文本框（超长截断，避免单次快照
     * 承载整页 ~200KB）。OkHttp 的 Kotlin/Okio/JCA 依赖是第三方兼容考面。
     */
    private void fetchBaiduHtml(final EditText output) {
        output.setText("OkHttp: requesting https://www.baidu.com/ ...");
        capabilityStatus.setText("OkHttp requesting baidu");
        new Thread(() -> {
            String text;
            try {
                OkHttpClient client = new OkHttpClient.Builder()
                        .connectTimeout(15, TimeUnit.SECONDS)
                        .readTimeout(20, TimeUnit.SECONDS)
                        .build();
                Request request = new Request.Builder()
                        .url("https://www.baidu.com/")
                        .header("User-Agent", "DexVM-target/1.0")
                        .build();
                try (Response response = client.newCall(request).execute()) {
                    String body = response.body() != null ? response.body().string() : "";
                    int total = body.length();
                    String shown = total > 6000
                            ? body.substring(0, 6000) + "\n…(truncated)"
                            : body;
                    text = "HTTP " + response.code() + " total=" + total + "\n" + shown;
                }
            } catch (Throwable failure) {
                text = "OkHttp error: " + failure;
            }
            final String result = text;
            Log.i("DexVmOkHttp", "result chars=" + result.length());
            mainHandler.post(() -> {
                output.setText(result);
                capabilityStatus.setText("OkHttp done chars=" + result.length());
            });
        }, "RuntimeOkHttpTask").start();
    }

    /** lib Dialog + Window：guest 异步弹框经 UI 桥作为叠加根物化。 */
    private void showGuestDialog() {
        final Dialog dialog = new Dialog(this);
        dialog.setTitle("VM dialog");
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setPadding(48, 48, 48, 48);
        column.setBackgroundColor(0xFFFFFFFF);
        TextView title = new TextView(this);
        title.setText("Dialog bridged from guest VM");
        title.setTextColor(0xFF161B26);
        title.setTextSize(18f);
        Button dismiss = new Button(this);
        dismiss.setText("Dismiss");
        dismiss.setOnClickListener(view -> dialog.dismiss());
        column.addView(title);
        column.addView(dismiss);
        dialog.setContentView(column);
        dialog.show();
        capabilityStatus.setText("Dialog shown");
    }

    private void wireRadio(RadioGroup group, int id) {
        final RadioButton button = (RadioButton) group.findViewById(id);
        if (button == null) return;
        button.setOnClickListener(view -> {
            // 直接派发路径兜底：performClick 语义下这里已是选中态，无操作。
            if (!button.isChecked()) button.toggle();
            capabilityStatus.setText("Radio clicked id=" + id);
        });
    }

    private void initializeMedia() {
        try {
            ImageView image = (ImageView) findViewById(R.id.runtime_image);
            image.setImageResource(R.drawable.runtime_photo);

            mediaVideo = (VideoView) findViewById(R.id.runtime_video);
            mediaVideo.setVideoPath(
                    "android.resource://com.coke.target/raw/runtime_video");
            mediaVideo.seekTo(200);
            mediaVideo.start();

            AudioManager stringAudio = (AudioManager)
                    getSystemService(Context.AUDIO_SERVICE);
            AudioManager classAudio =
                    getSystemService(AudioManager.class);
            if (classAudio == null || classAudio != stringAudio) {
                throw new IllegalStateException(
                        "Context.getSystemService identity mismatch");
            }
            AudioDeviceInfo[] outputs = classAudio.getDevices(
                    AudioManager.GET_DEVICES_OUTPUTS);
            AudioDeviceInfo speaker = null;
            for (AudioDeviceInfo output : outputs) {
                if (output.getType() ==
                        AudioDeviceInfo.TYPE_BUILTIN_SPEAKER) {
                    speaker = output;
                    break;
                }
            }

            audioPlayer = new MediaPlayer();
            audioPlayer.setDataSource(
                    "android.resource://com.coke.target/raw/runtime_tone");
            audioPlayer.prepare();
            audioPlayer.setLooping(true);
            audioPlayer.setVolume(0.15f, 0.15f);
            boolean speakerPreferred = speaker != null &&
                    audioPlayer.setPreferredDevice(speaker);
            audioPlayer.start();
            mediaStatus.setText(
                    "Media ready image video audio duration="
                            + audioPlayer.getDuration()
                            + " orientation="
                            + getResources().getConfiguration().orientation
                            + " devices=" + outputs.length
                            + " speaker=" + (speaker != null)
                            + " routed=" + speakerPreferred);
        } catch (Exception failure) {
            mediaStatus.setText("Media error: " + failure.getMessage());
        }
    }

    private void startNettyHttpServer() {
        if (nettyServerRunning) {
            nettyStatus.setText("Netty HTTP already running on 18080");
            return;
        }
        nettyServerRunning = true;
        mainHandler.post(() -> nettyStatus.setText(
                "Starting Netty HTTP on 127.0.0.1:18080"));
        runtimePanel.updateNettyStatus("Netty HTTP starting");
        mainHandler.postDelayed(() -> {
            if (nettyServerRunning) {
                nettyStatus.setText(
                        "Netty HTTP listening on 127.0.0.1:18080");
                runtimePanel.updateNettyStatus(
                        "Netty HTTP listening on 127.0.0.1:18080");
            }
        }, 4000);
        new Thread(() -> {
            int result = RuntimeNettyFastHttpServerFixture
                    .fastHttpServerFlow(18080, 600);
            nettyServerRunning = false;
            mainHandler.post(() -> {
                nettyStatus.setText(
                        "Netty HTTP stopped result=" + result);
                runtimePanel.updateNettyStatus(
                        "Netty HTTP stopped result=" + result);
            });
        }, "RuntimeNettyHttpServer").start();
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        outState.putString("probe_mode", "android-specials");
        outState.putInt("ui_mode", uiMode);
        outState.putBoolean("handler_ready", mainHandler != null);
        super.onSaveInstanceState(outState);
    }

    @Override
    protected void onStart() {
        super.onStart();
        RuntimeLifecycleApp.lifecycleLog += "activity.onStart;";
        Log.i("DexVmTargetLifecycle", "activity.onStart");
        if (statusView != null) statusView.setText(R.string.status_started);
    }

    @Override
    protected void onResume() {
        super.onResume();
        RuntimeLifecycleApp.lifecycleLog += "activity.onResume;";
        Log.i("DexVmTargetLifecycle", "activity.onResume");
        if (statusView != null) statusView.setText(R.string.status_resumed);
    }

    @Override
    protected void onPause() {
        super.onPause();
        statusView.setText(R.string.status_paused);
    }

    @Override
    protected void onStop() {
        super.onStop();
        statusView.setText(R.string.status_stopped);
    }

    @Override
    protected void onDestroy() {
        if (statusView != null) statusView.setText(R.string.status_destroyed);
        if (mediaVideo != null) {
            mediaVideo.stopPlayback();
        }
        if (hlsVideo != null) {
            hlsVideo.stopPlayback();
        }
        if (audioPlayer != null) {
            audioPlayer.release();
            audioPlayer = null;
        }
        super.onDestroy();
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_UP) {
            touchCount++;
            featureSwitch.setChecked(!featureSwitch.isChecked());
            featureProgress.setProgress(Math.min(100, touchCount * 20));
            runtimePanel.updateTouchCount(touchCount);
        }
        statusView.setText(
                getString(R.string.status_touched)
                        + " action=" + action
                        + " x=" + Math.round(event.getX())
                        + " y=" + Math.round(event.getY()));
        return true;
    }
}
