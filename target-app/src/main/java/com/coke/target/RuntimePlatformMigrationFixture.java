package com.coke.target;

import java.util.UUID;
import org.json.JSONArray;
import org.json.JSONObject;

public final class RuntimePlatformMigrationFixture {
    public int uuidAndPropertiesFlow() {
        UUID expected = UUID.fromString(
                "123e4567-e89b-12d3-a456-426614174000");
        UUID equivalent = UUID.fromString(
                "123e4567-e89b-12d3-a456-426614174000");
        UUID random = UUID.randomUUID();
        if (expected.compareTo(equivalent) != 0) {
            return -1;
        }
        if (random.getMostSignificantBits() == 0
                || random.getLeastSignificantBits() == 0) {
            return -2;
        }
        if (!"123e4567-e89b-12d3-a456-426614174000"
                .equals(expected.toString())) {
            return -3;
        }
        UUID named = UUID.nameUUIDFromBytes(
                new byte[] {
                    'c', 'o', 'm', '.', 'c', 'o', 'k', 'e',
                    '.', 't', 'e', 's', 't',
                });
        if (!"1e3fd79b-cb5b-3ec7-a4fb-d0c2779d0d59"
                .equals(named.toString())) {
            return -4;
        }
        String[] parts = "a,,b,".split(",");
        if (parts.length != 3 || !"a".equals(parts[0]) ||
                !parts[1].isEmpty() || !"b".equals(parts[2])) {
            return -5;
        }
        String[] preserved = "a,,b,".split(",", -1);
        if (preserved.length != 4 || !preserved[3].isEmpty()) {
            return -6;
        }
        String[] limited = "a,b,c".split(",", 2);
        if (limited.length != 2 || !"b,c".equals(limited[1])) {
            return -7;
        }
        String[] empty = "".split(",");
        if (empty.length != 1 || !empty[0].isEmpty()) {
            return -8;
        }
        return 91;
    }

    public int jsonFlow() throws org.json.JSONException {
        JSONObject object = new JSONObject(
                "{\"a\":1,\"nested\":{\"b\":true},"
                        + "\"array\":[7,8,\"x\"]}");
        if (object.length() != 3) {
            return -10;
        }
        if (object.getInt("a") != 1) {
            return -1;
        }
        if (!object.getJSONObject("nested").getBoolean("b")) {
            return -2;
        }
        JSONArray array = object.getJSONArray("array");
        if (array.length() != 3 || array.getInt(1) != 8) {
            return -3;
        }
        object.put("c", 9);
        object.remove("a");
        if (object.length() != 3 || !object.has("c")) {
            return -4;
        }
        JSONObject copy = new JSONObject(object.toString());
        if (copy.getInt("c") != 9
                || copy.getJSONArray("array").getString(2)
                        .equals("x") == false) {
            return -5;
        }
        return 92;
    }
}
