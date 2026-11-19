package com.coke.target;

import android.content.Intent;
import android.os.Bundle;

public final class RuntimeIntentFixture {
    public int intentFlow() {
        Intent intent = new Intent();
        intent.putExtra("string", "value")
                .putExtra("int", 17)
                .putExtra("long", 9000000000L)
                .putExtra("boolean", true)
                .putExtra("float", 1.5f)
                .putExtra("double", 2.5d);
        if (!"value".equals(intent.getStringExtra("string")) ||
                intent.getIntExtra("int", -1) != 17 ||
                intent.getLongExtra("long", -1L) != 9000000000L ||
                !intent.getBooleanExtra("boolean", false) ||
                intent.getFloatExtra("float", 0.0f) != 1.5f ||
                intent.getDoubleExtra("double", 0.0d) != 2.5d ||
                !intent.hasExtra("int")) {
            return -1;
        }

        Bundle bundle = new Bundle();
        bundle.putString("bundle", "nested");
        bundle.putInt("number", 21);
        intent.putExtra("nested", bundle);
        intent.putExtras(bundle);
        Bundle copied = intent.getBundleExtra("nested");
        if (intent.getIntExtra("number", -1) != 21 ||
                copied == null ||
                !"nested".equals(copied.getString("bundle"))) {
            return -2;
        }

        intent.setAction("com.coke.action.TEST");
        intent.setPackage("com.coke.target");
        intent.addFlags(0x10000000).addFlags(0x04000000);
        if (!"com.coke.action.TEST".equals(intent.getAction()) ||
                !"com.coke.target".equals(intent.getPackage()) ||
                (intent.getFlags() & 0x14000000) != 0x14000000) {
            return -3;
        }
        intent.removeExtra("int");
        if (intent.hasExtra("int") || intent.getExtras() == null) {
            return -4;
        }
        if (!intent.toString().contains("com.coke.action.TEST")) {
            return -5;
        }
        return 98;
    }
}
