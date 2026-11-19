package com.coke.target;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.view.WindowManager;
import android.graphics.Color;
import android.text.TextUtils;
import java.io.File;
import java.io.InputStream;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.PrintWriter;
import java.io.RandomAccessFile;
import java.io.StringReader;
import java.io.StringWriter;
import java.lang.reflect.ParameterizedType;
import java.lang.reflect.Type;
import java.lang.reflect.Modifier;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.nio.MappedByteBuffer;
import java.nio.channels.FileChannel;
import java.time.LocalDate;
import java.util.Arrays;
import java.util.Calendar;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.OptionalDouble;
import java.util.OptionalInt;
import java.util.OptionalLong;
import java.util.Set;
import java.util.StringJoiner;
import java.util.StringTokenizer;
import java.util.concurrent.DelayQueue;
import java.util.concurrent.Delayed;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.TimeZone;
import java.util.concurrent.TimeUnit;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** Exercises the standard Java and PackageManager boot-library paths. */
public final class RuntimeLibraryCompatibilityFixture {
    private static class Box<T> {
    }

    private static final class DelayedValue implements Delayed {
        private final long readyAtMillis;
        private final int value;

        DelayedValue(long readyAtMillis, int value) {
            this.readyAtMillis = readyAtMillis;
            this.value = value;
        }

        @Override
        public long getDelay(TimeUnit unit) {
            return unit.convert(
                    Math.max(0, readyAtMillis - System.currentTimeMillis()),
                    TimeUnit.MILLISECONDS);
        }

        @Override
        public int compareTo(Delayed other) {
            return Long.compare(
                    getDelay(TimeUnit.MILLISECONDS),
                    other.getDelay(TimeUnit.MILLISECONDS));
        }
    }

    @Retention(RetentionPolicy.RUNTIME)
    private @interface Marker {
        Mode value();
    }

    private enum Mode {
        FIRST,
        SECOND,
    }

    @Marker(Mode.SECOND)
    private void annotatedMethod() {
    }

    public int arrayAndGenericReflectionFlow() {
        int[] left = {1, 2, 3};
        int[] right = {1, 2, 3};
        int[] different = {1, 2, 4};
        if (!Arrays.equals(left, right) || Arrays.equals(left, different)) {
            return -1;
        }
        Type superclass = new Box<String>() {
        }.getClass().getGenericSuperclass();
        if (!(superclass instanceof ParameterizedType)) {
            return -2;
        }
        Type[] arguments =
                ((ParameterizedType) superclass).getActualTypeArguments();
        if (arguments.length != 1 || arguments[0] != String.class) {
            return -3;
        }
        return 109;
    }

    public int mappedBufferFlow() throws Exception {
        File file = File.createTempFile("vm-buffer", ".bin");
        RandomAccessFile random = new RandomAccessFile(file, "rw");
        try {
            random.setLength(64);
            MappedByteBuffer buffer = random.getChannel().map(
                    FileChannel.MapMode.READ_WRITE,
                    0,
                    64);
            buffer.putInt(0, 0x01020304);
            byte[] bytes = {5, 6, 7, 8};
            buffer.position(4);
            buffer.put(bytes, 0, bytes.length);
            byte[] restored = new byte[4];
            buffer.position(4);
            buffer.get(restored, 0, restored.length);
            StringWriter text = new StringWriter();
            PrintWriter writer = new PrintWriter(text);
            writer.flush();
            if (buffer.getInt(0) != 0x01020304) {
                return -1;
            }
            if (restored[0] != 5 || restored[3] != 8) return -2;
        } finally {
            random.close();
            file.delete();
        }
        return 110;
    }

    public int delayedQueueFlow() throws Exception {
        DelayQueue<DelayedValue> queue = new DelayQueue<DelayedValue>();
        long started = System.currentTimeMillis();
        DelayedValue queued = new DelayedValue(started + 500, 111);
        if (queued.getDelay(TimeUnit.MILLISECONDS) < 40) return -4;
        queue.offer(queued);
        if (queue.poll() != null) return -3;
        DelayedValue value = queue.take();
        if (value.value != 111) return -2;
        return value.value;
    }

    public int methodAnnotationFlow() throws Exception {
        java.lang.reflect.Method method =
                getClass().getDeclaredMethod("annotatedMethod");
        Marker marker = method.getAnnotation(Marker.class);
        if (marker == null || marker.value() != Mode.SECOND) return -1;
        return 112;
    }

    public int objectsAndModifierFlow() {
        String value = Objects.requireNonNull("runtime");
        if (!Objects.equals(value, "runtime") ||
                Objects.hashCode(null) != 0 ||
                !Modifier.isPublic(
                        RuntimeLibraryCompatibilityFixture.class
                                .getModifiers()) ||
                Modifier.isStatic(
                        RuntimeLibraryCompatibilityFixture.class
                                .getModifiers())) {
            return -1;
        }
        return 113;
    }

    public int valueUtilitiesFlow() {
        List<String> emptyList = Collections.emptyList();
        if (emptyList != Collections.<String>emptyList() ||
                !emptyList.isEmpty() ||
                emptyList.size() != 0 ||
                emptyList.iterator().hasNext() ||
                !emptyList.containsAll(Collections.emptySet()) ||
                emptyList.toArray().length != 0 ||
                !"[]".equals(emptyList.toString())) {
            return -1;
        }
        boolean listRejected = false;
        try {
            emptyList.add("value");
        } catch (UnsupportedOperationException expected) {
            listRejected = true;
        }
        if (!listRejected) return -2;

        Set<Integer> emptySet = Collections.emptySet();
        if (emptySet != Collections.<Integer>emptySet() ||
                !emptySet.isEmpty() ||
                emptySet.contains(1) ||
                !emptySet.containsAll(Collections.emptyList()) ||
                !"{}".equals(emptySet.toString())) {
            return -3;
        }
        boolean setRejected = false;
        try {
            emptySet.add(1);
        } catch (UnsupportedOperationException expected) {
            setRejected = true;
        }
        if (!setRejected) return -4;

        Map<String, String> emptyMap = Collections.emptyMap();
        if (emptyMap != Collections.<String, String>emptyMap() ||
                !emptyMap.isEmpty() ||
                emptyMap.get("missing") != null ||
                emptyMap.remove("missing") != null ||
                !emptyMap.keySet().isEmpty() ||
                !emptyMap.values().isEmpty() ||
                !emptyMap.entrySet().isEmpty()) {
            return -5;
        }
        boolean mapRejected = false;
        try {
            emptyMap.put("key", "value");
        } catch (UnsupportedOperationException expected) {
            mapRejected = true;
        }
        if (!mapRejected) return -6;

        StringJoiner joined = new StringJoiner(":", "[", "]");
        joined.add("a").add("b");
        joined.merge(new StringJoiner(",", "<", ">").add("c").add("d"));
        if (!"[a:b:c,d]".equals(joined.toString()) ||
                joined.length() != 9 ||
                !"empty".equals(
                        new StringJoiner(",").setEmptyValue("empty")
                                .toString())) {
            return -7;
        }

        StringTokenizer tokens =
                new StringTokenizer("alpha,beta;;gamma", ",;");
        if (tokens.countTokens() != 3 ||
                !"alpha".equals(tokens.nextToken()) ||
                !"beta".equals(tokens.nextToken()) ||
                !"gamma".equals(tokens.nextToken()) ||
                tokens.hasMoreTokens()) {
            return -8;
        }

        int[] observedLength = {0};
        Optional<String> selected =
                Optional.of(" vm ")
                        .map(value -> value.trim())
                        .filter(value -> value.equals("vm"));
        selected.ifPresent(value -> observedLength[0] = value.length());
        if (!selected.isPresent() ||
                !"vm".equals(selected.get()) ||
                observedLength[0] != 2 ||
                Optional.ofNullable(null).isPresent() ||
                !"fallback".equals(
                        Optional.empty().orElseGet(() -> "fallback")) ||
                OptionalInt.of(7).getAsInt() != 7 ||
                OptionalLong.of(8L).getAsLong() != 8L ||
                OptionalDouble.of(2.5d).getAsDouble() != 2.5d) {
            return -9;
        }
        return 115;
    }

    public int androidValueLibraryFlow() {
        int color = Color.rgb(1, 2, 3);
        if (color != 0xff010203 ||
                Color.parseColor("#050607") != 0xff050607) {
            return -1;
        }
        if (!TextUtils.isEmpty("") ||
                !TextUtils.isEmpty(null) ||
                TextUtils.isEmpty("value") ||
                !TextUtils.equals("value", "value") ||
                !"a,b".equals(TextUtils.join(",", new String[]{"a", "b"}))) {
            return -2;
        }
        return 114;
    }

    public int calendarFlow() {
        Calendar calendar = Calendar.getInstance(
                TimeZone.getTimeZone("UTC"));
        calendar.set(1970, Calendar.JANUARY, 2, 3, 4, 5);
        if (calendar.getTimeInMillis() != 97_445_000L ||
                calendar.get(Calendar.YEAR) != 1970 ||
                calendar.get(Calendar.MONTH) != Calendar.JANUARY ||
                calendar.get(Calendar.DAY_OF_MONTH) != 2 ||
                calendar.get(Calendar.HOUR_OF_DAY) != 3) {
            return -1;
        }
        calendar.add(Calendar.DAY_OF_MONTH, -1);
        if (calendar.get(Calendar.DAY_OF_MONTH) != 1 ||
                calendar.getTimeInMillis() != 11_045_000L) {
            return -2;
        }
        return 105;
    }

    public int localDateFlow() {
        LocalDate leapDay = LocalDate.of(2024, 2, 29);
        LocalDate nextDay = leapDay.plusDays(1);
        LocalDate parsed = LocalDate.parse("2000-02-29");
        if (!"2024-03-01".equals(nextDay.toString()) ||
                !parsed.isLeapYear() ||
                parsed.lengthOfMonth() != 29 ||
                leapDay.plusYears(1).getDayOfMonth() != 28 ||
                LocalDate.ofEpochDay(0).getYear() != 1970 ||
                LocalDate.ofEpochDay(0).getMonthValue() != 1 ||
                LocalDate.ofEpochDay(0).getDayOfMonth() != 1 ||
                nextDay.toEpochDay() != leapDay.toEpochDay() + 1) {
            return -1;
        }
        return 104;
    }

    public static String packageManagerSummary(Context context) {
        try {
            PackageInfo info = context.getPackageManager().getPackageInfo(
                    context.getPackageName(),
                    0);
            ApplicationInfo application = info.applicationInfo;
            return "PM=" + info.versionName + "/" +
                    info.getLongVersionCode() + " sdk=" +
                    application.targetSdkVersion;
        } catch (PackageManager.NameNotFoundException failure) {
            return "PM=missing";
        }
    }

    public static String contextSummary(Context context) throws Exception {
        File files = context.getFilesDir();
        File cache = context.getCacheDir();
        File codeCache = context.getCodeCacheDir();
        File noBackup = context.getNoBackupFilesDir();
        File data = context.getDataDir();
        File externalFiles = context.getExternalFilesDir("documents");
        File externalCache = context.getExternalCacheDir();
        File database = context.getDatabasePath("runtime.db");
        File streamPath = context.getFileStreamPath("context-probe.txt");
        if (files == null || cache == null || codeCache == null ||
                noBackup == null || data == null || externalFiles == null ||
                externalCache == null || database == null ||
                streamPath == null || context.getClassLoader() == null ||
                context.getContentResolver() == null ||
                context.getAssets() == null ||
                context.getParams() == null ||
                context.getUpdateableFlags() == null ||
                Context.BindServiceFlags.of(1L) == null ||
                context.getPackageCodePath() == null ||
                context.getPackageResourcePath() == null ||
                !context.getPackageName().equals(
                        context.getOpPackageName()) ||
                context.getApplicationInfo() == null ||
                context.createConfigurationContext(
                        context.getResources().getConfiguration()) == null ||
                context.createContext(context.getParams()) == null ||
                context.createDeviceContext(
                        Context.DEVICE_ID_DEFAULT) == null ||
                context.createDeviceProtectedStorageContext() == null ||
                context.getMainExecutor() == null ||
                context.getDeviceId() != Context.DEVICE_ID_DEFAULT ||
                context.isRestricted()) {
            return "Context=invalid";
        }

        WindowManager stringWindow = (WindowManager)
                context.getSystemService(Context.WINDOW_SERVICE);
        WindowManager classWindow =
                context.getSystemService(WindowManager.class);
        if (classWindow == null || classWindow != stringWindow) {
            return "Context=window-service";
        }

        byte[] content = {'v', 'm', '-', 'c', 't', 'x'};
        try (FileOutputStream output =
                     context.openFileOutput("context-probe.txt", 0)) {
            output.write(content);
        }
        byte[] restored = new byte[content.length];
        try (FileInputStream input =
                     context.openFileInput("context-probe.txt")) {
            if (input.read(restored) != restored.length) {
                return "Context=short-read";
            }
        }
        for (int index = 0; index < content.length; index++) {
            if (restored[index] != content[index]) {
                return "Context=file-mismatch";
            }
        }
        boolean listed = false;
        for (String name : context.fileList()) {
            listed |= "context-probe.txt".equals(name);
        }
        if (!listed || !context.deleteFile("context-probe.txt") ||
                context.getFileStreamPath("context-probe.txt").exists()) {
            return "Context=file-list";
        }

        try (InputStream asset =
                     context.getAssets().open("activity_runtime.xml")) {
            if (asset.read() < 0) {
                return "Context=asset";
            }
        }

        SharedPreferences preferences = context.getSharedPreferences(
                "context-probe",
                Context.MODE_PRIVATE);
        if (!preferences.edit().putString("mode", "native").commit() ||
                !"native".equals(preferences.getString("mode", ""))) {
            return "Context=preferences";
        }
        context.deleteSharedPreferences("context-probe");

        Uri uri = Uri.parse("content://com.coke.target/context");
        int uriMode = Intent.FLAG_GRANT_READ_URI_PERMISSION;
        context.grantUriPermission(
                "com.coke.other",
                uri,
                uriMode);
        if (context.checkUriPermission(
                uri,
                android.os.Process.myPid(),
                android.os.Process.myUid(),
                uriMode) != PackageManager.PERMISSION_GRANTED) {
            return "Context=uri";
        }
        context.revokeUriPermission(uri, uriMode);

        final int[] broadcasts = {0};
        BroadcastReceiver receiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context source, Intent intent) {
                broadcasts[0]++;
            }
        };
        context.registerReceiver(
                receiver,
                new IntentFilter("com.coke.target.CONTEXT_PROBE"));
        context.sendBroadcast(
                new Intent("com.coke.target.CONTEXT_PROBE"));
        context.unregisterReceiver(receiver);
        if (broadcasts[0] != 1) {
            return "Context=broadcast";
        }

        if (context.getAssets().list("").length == 0 ||
                context.getClassLoader().loadClass(
                        "com.coke.target.MainActivity") == null ||
                context.getColor(R.color.runtime_background) == 0 ||
                context.getColorStateList(
                        R.color.runtime_background) == null ||
                context.getDrawable(R.drawable.runtime_photo) == null ||
                context.obtainStyledAttributes(
                        new int[]{android.R.attr.textColor}) == null ||
                context.getText(R.string.status_started) == null ||
                context.getTheme() == null ||
                context.getSystemServiceName(
                        android.media.AudioManager.class) == null) {
            return "Context=resources";
        }
        android.database.sqlite.SQLiteDatabase sqliteDatabase =
                context.openOrCreateDatabase(
                "context-probe.db",
                Context.MODE_PRIVATE,
                null);
        if (sqliteDatabase == null) {
            return "Context=database";
        }
        sqliteDatabase.close();
        return "Context=ok";
    }

    public int regexFlow() {
        Pattern pattern = Pattern.compile("([a-z]+)-(\\d+)");
        Matcher matcher = pattern.matcher("vm-42");
        if (!matcher.find() || !"vm".equals(matcher.group(1)) ||
                !"42".equals(matcher.group(2)) ||
                matcher.start(1) != 0 || matcher.end(2) != 5) {
            return -1;
        }
        if (!"vm-42".matches("[a-z]+-\\d+") ||
                !"a-b-c".replaceAll("-", ":").equals("a:b:c") ||
                "a-b-c".split("-").length != 3) {
            return -2;
        }
        return 101;
    }

    public int stringReaderWriterFlow() throws Exception {
        StringWriter writer = new StringWriter();
        writer.write('v');
        writer.write("m");
        char[] suffix = {'-', 'V', 'M'};
        writer.write(suffix, 0, suffix.length);
        if (!"vm-VM".equals(writer.toString())) {
            return -1;
        }

        StringReader reader = new StringReader(writer.toString());
        reader.mark(16);
        char[] first = new char[2];
        if (reader.read(first, 0, first.length) != 2 ||
                first[0] != 'v' || first[1] != 'm') {
            return -2;
        }
        reader.reset();
        if (reader.read() != 'v' || reader.skip(2) != 2 ||
                reader.read() != 'V') {
            return -3;
        }
        reader.close();
        return 102;
    }

    public int mapFlow() {
        Map<String, Integer> values = new HashMap<String, Integer>(2);
        values.put("a", 1);
        values.put("b", 2);
        values.put("a", 3);
        if (!values.containsKey("a") || !values.containsValue(2) ||
                values.size() != 2 || values.get("a") != 3 ||
                values.remove("b") != 2 || values.size() != 1) {
            return -1;
        }

        LinkedHashMap<String, String> ordered =
                new LinkedHashMap<String, String>();
        ordered.put("first", "1");
        ordered.put("second", "2");
        StringBuilder keys = new StringBuilder();
        for (Map.Entry<String, String> entry : ordered.entrySet()) {
            keys.append(entry.getKey()).append('=');
            keys.append(entry.getValue()).append(';');
        }
        if (!"first=1;second=2;".equals(keys.toString())) {
            return -2;
        }
        ordered.clear();
        if (!ordered.isEmpty()) {
            return -3;
        }
        return 106;
    }

    public int packageManagerFlow(Context context) throws Exception {
        PackageManager manager = context.getPackageManager();
        String packageName = context.getPackageName();
        PackageInfo info = manager.getPackageInfo(packageName, 0);
        ApplicationInfo application =
                manager.getApplicationInfo(packageName, 0);
        if (!packageName.equals(info.packageName) ||
                !"1.0".equals(info.versionName) ||
                info.getLongVersionCode() != 1L ||
                info.applicationInfo == null ||
                !packageName.equals(info.applicationInfo.packageName) ||
                info.applicationInfo.targetSdkVersion != 37 ||
                info.applicationInfo.minSdkVersion != 26 ||
                application.nativeLibraryDir == null ||
                application.nativeLibraryDir.isEmpty() ||
                !application.dataDir.endsWith(packageName)) {
            return -1;
        }
        String[] packages = manager.getPackagesForUid(application.uid);
        if (packages == null || packages.length != 1 ||
                !packageName.equals(packages[0])) {
            return -2;
        }
        return 103;
    }
}
