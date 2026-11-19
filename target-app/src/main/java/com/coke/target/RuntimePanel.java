package com.coke.target;

import android.content.Context;
import android.view.Gravity;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * Custom ViewGroup built by target code, not by the VM renderer.
 */
public final class RuntimePanel extends LinearLayout {
    private final TextView label;
    private final TextView fileLabel;
    private final TextView socketLabel;
    private final TextView monitorLabel;
    private final TextView httpLabel;
    private final TextView nettyLabel;
    private final TextView asyncLabel;
    private final TextView cycleLabel;
    private int score;

    public RuntimePanel(Context context) {
        super(context);
        setOrientation(VERTICAL);
        setGravity(Gravity.CENTER);

        label = new TextView(context);
        label.setText("Programmatic VM panel");
        label.setGravity(Gravity.CENTER);
        addView(label);

        fileLabel = new TextView(context);
        fileLabel.setText("Guest file pending");
        addView(fileLabel);

        socketLabel = new TextView(context);
        socketLabel.setText("Guest socket pending");
        addView(socketLabel);

        monitorLabel = new TextView(context);
        monitorLabel.setText("Guest monitor pending");
        addView(monitorLabel);

        httpLabel = new TextView(context);
        httpLabel.setText("Guest HTTP pending");
        addView(httpLabel);

        nettyLabel = new TextView(context);
        nettyLabel.setText("Netty HTTP pending");
        addView(nettyLabel);

        asyncLabel = new TextView(context);
        asyncLabel.setText("Guest Handler pending");
        addView(asyncLabel);

        cycleLabel = new TextView(context);
        cycleLabel.setText("UI cycle 0: VISIBLE alpha=1.0 selected=false");
        addView(cycleLabel);
    }

    public void updateScore(int value) {
        score = value;
        label.setText("Programmatic VM panel: " + score);
    }

    public void updateTouchCount(int value) {
        label.setText("Touch count: " + value);
    }

    public void updateFileStatus(String value) {
        fileLabel.setText(value);
    }

    public void updateSocketStatus(String value) {
        socketLabel.setText(value);
    }

    public void updateMonitorStatus(String value) {
        monitorLabel.setText(value);
    }

    public void updateHttpStatus(String value) {
        httpLabel.setText(value);
    }

    public void updateNettyStatus(String value) {
        nettyLabel.setText(value);
    }

    public void updateAsyncStatus(String value) {
        asyncLabel.setText(value);
    }

    public void updateUiCycle(String value) {
        cycleLabel.setText(value);
    }
}
