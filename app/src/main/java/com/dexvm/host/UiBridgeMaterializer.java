package com.dexvm.host;

import android.content.Context;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.Drawable;
import android.text.Editable;
import android.text.TextWatcher;
import android.util.Log;
import android.util.TypedValue;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.RadioButton;
import android.widget.RelativeLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.ToggleButton;
import android.widget.VideoView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * UI 桥接 P1 宿主物化器：桥轨 View 树 envelope（ViewSnapshot/ViewPatch + draw
 * 命令流，线格式镜像 bridge/ui_bridge_protocol.h）-> 真 android.view.View 树
 * （TextView/ImageView/LinearLayout/ViewGroup -> HWUI 出画）。
 *
 * <p>结构保真口径：guest 布局引擎（framework/render.cc traversal）产出的 bounds
 * 为绝对坐标——物化容器按 bounds 绝对摆放子 View（不重跑宿主 measure 语义），
 * 文本/颜色等字段态直接映射宿主控件（像素不作金样，随宿主渲染器漂移）。
 * 属性 patch 增量应用（Update/加减节点）；点击回灌：宿主点击 -> viewId ->
 * guest listener 注入（NativeBridge.nativeDispatchViewClick）。
 *
 * <p>通用 framework 桥接形态：本解析面对 HostHandle/PropValue 泛型编码（kind
 * 扩展后续 Context 服务/Window/资源对象复用）；P1 只物化 kind=View。
 */
public final class UiBridgeMaterializer {
    private static final String TAG = "DexVmUiBridge";

    // ---- 协议常量（镜像 bridge/ui_bridge_protocol.h）----
    private static final int MAGIC = 0x52424955;
    private static final int VERSION = 1;
    private static final int KIND_FULL = 1;
    private static final int KIND_PATCH = 2;
    private static final int NO_OBJECT = -1; // kNoObject = 0xFFFFFFFF

    private static final int PATCH_ADD = 1;
    private static final int PATCH_REMOVE = 2;
    private static final int PATCH_UPDATE = 3;

    private static final int PROP_TEXT = 1;
    private static final int PROP_VISIBILITY = 2;
    private static final int PROP_BOUNDS = 3;
    private static final int PROP_PADDING = 4;
    private static final int PROP_TEXT_SIZE = 5;
    private static final int PROP_TEXT_COLOR = 6;
    private static final int PROP_BG_COLOR = 7;
    private static final int PROP_ALPHA = 8;
    private static final int PROP_SCROLL = 9;
    private static final int PROP_MEASURED = 10;
    private static final int PROP_LAYOUT_PARAMS = 11;
    private static final int PROP_HAS_CLICK = 12;
    private static final int PROP_TRANSLATION = 13;
    private static final int PROP_GRAVITY = 14;
    private static final int PROP_IMAGE_RESOURCE = 15;
    private static final int PROP_IMAGE_BYTES = 16;
    private static final int PROP_PROGRESS = 17;
    private static final int PROP_VIDEO_PATH = 18;
    private static final int PROP_VIDEO_SEEK = 19;
    private static final int PROP_VIDEO_PLAYING = 20;
    private static final int PROP_LAYOUT_GRAVITY = 21;
    private static final int PROP_ORIENTATION = 22;
    private static final int PROP_LAYOUT_MARGINS = 23;
    private static final int PROP_LAYOUT_WEIGHT = 24;
    private static final int PROP_LAYOUT_KIND = 25;
    private static final int PROP_WINDOW_DIM = 26;
    private static final int PROP_CHECKED = 27;
    private static final int PROP_HAS_LONG_CLICK = 28;

    private static final int TYPE_INT = 1;
    private static final int TYPE_FLOAT = 2;
    private static final int TYPE_BOOL = 3;
    private static final int TYPE_STRING = 4;
    private static final int TYPE_RECT = 5;
    private static final int TYPE_POINT = 6;
    private static final int TYPE_HANDLE = 7;
    private static final int TYPE_BYTES = 8;

    // ---- 协议数据形 ----

    static final class Prop {
        int key;
        int type;
        long i;          // INT/BOOL/HANDLE(object_id<<8|kind)/POINT(x,y 打包不在此)
        float f;
        String s;
        byte[] bytes;
        int[] rect = new int[4];

        long asLong() { return i; }
        int asInt() { return (int) i; }
    }

    static final class Node {
        int objectId;
        int handleKind;
        int parentId;
        int childIndex;
        int guestViewId = -1;
        String className = "";
        final List<Prop> props = new ArrayList<>();
        final List<Integer> children = new ArrayList<>();

        Prop prop(int key) {
            for (Prop p : props) if (p.key == key) return p;
            return null;
        }
    }

    static final class PatchOp {
        int op;
        int objectId;
        int handleKind;
        int parentId;
        int childIndex;
        int guestViewId = -1;
        String className = "";
        final List<Prop> props = new ArrayList<>();
    }

    /** apply() 产物：物化根 + 协议面统计（revision 脏计数随 patch 累积）。 */
    public static final class Result {
        public View root;
        public int revision;
        public boolean patch;
        public int nodeCount;
        public int drawCommands;
        public int dirtyCount;
    }

    // ---- 状态 ----

    private final Context context;
    private final Map<Integer, View> views = new HashMap<>(); // object_id -> host View
    private final Map<Integer, Node> nodes = new HashMap<>();
    // guest traversal 已产出有效绝对几何的节点；其余节点交给宿主 measure/layout
    // （视图层已桥接：宿主真 View 自量，guest 只提供结构与字段态）。
    private final Map<View, Boolean> absoluteViews = new IdentityHashMap<>();
    private final Map<View, Integer> childGravity = new IdentityHashMap<>();
    private final Map<View, Float> childWeight = new IdentityHashMap<>();
    // 交互回灌挂接面：TextWatcher / SeekBar listener / long-click 只装一次。
    private final Set<View> textWatchersInstalled =
            Collections.newSetFromMap(new IdentityHashMap<View, Boolean>());
    private final Set<View> seekListenersInstalled =
            Collections.newSetFromMap(new IdentityHashMap<View, Boolean>());
    private final Set<View> longClickInstalled =
            Collections.newSetFromMap(new IdentityHashMap<View, Boolean>());
    // 快照回写文本时抑制 TextWatcher 回灌（避免 host->guest 回声）。
    private final Set<View> suppressTextEvents =
            Collections.newSetFromMap(new IdentityHashMap<View, Boolean>());
    private View root;
    // Dialog 叠加层：窗口根（parentId 无父）出现时，用一个 wrapper 托管
    // Activity 根 + scrim + 窗口内容根；Dialog 对象本身不出 guest。
    private HostFrameLayout overlayWrapper;
    private final Map<Integer, View> dialogScrims = new HashMap<>();
    private int appliedRevision = 0;
    private int dirtyCount = 0;
    private String apkPath;
    // guest APK 资源 drawable 装载（XML vector / bitmap；ImageDrawable 物化）。
    private ApkDrawableLoader drawableLoader;
    private final Map<Integer, String> appliedVideoPath = new HashMap<>();

    public UiBridgeMaterializer(Context context) {
        // guest DisplayMetrics 固定 320dpi（framework_app.cc），宿主模拟器约
        // 420dpi；不覆盖会让 Switch/Button/EditText 等宿主控件按宿主密度
        // 自量，比 guest 布局大约 30%。物化控件统一用 guest 密度出画。
        Configuration cfg = new Configuration(context.getResources().getConfiguration());
        cfg.densityDpi = 320;
        this.context = context.createConfigurationContext(cfg);
    }

    /** 目标 APK 路径（VideoView 的 android.resource:// 源从 APK 抽包真播放）。 */
    public void setApkPath(String path) {
        this.apkPath = path;
        this.drawableLoader = path != null ? new ApkDrawableLoader(context, path) : null;
    }

    public int appliedRevision() { return appliedRevision; }

    public int dirtyCount() { return dirtyCount; }

    public View rootView() { return root; }

    /** 全量/增量 envelope 应用 -> 物化（增量为 patch 时就地更新现有树）。 */
    public Result apply(byte[] envelope) {
        if (envelope == null || envelope.length < 20) {
            throw new IllegalStateException("ui-bridge: empty/short envelope");
        }
        ByteBuffer buf = ByteBuffer.wrap(envelope).order(ByteOrder.LITTLE_ENDIAN);
        int magic = buf.getInt();
        int version = buf.getShort() & 0xFFFF;
        int kind = buf.get() & 0xFF;
        buf.get(); // reserved
        int revision = buf.getInt();
        int snapshotBytes = buf.getInt();
        int drawBytes = buf.getInt();
        if (magic != MAGIC || version != VERSION) {
            throw new IllegalStateException("ui-bridge: bad magic/version " + Integer.toHexString(magic));
        }
        int snapshotEnd = buf.position() + snapshotBytes;
        if (snapshotEnd + drawBytes != envelope.length) {
            throw new IllegalStateException("ui-bridge: segment length mismatch");
        }

        Result result = new Result();
        result.revision = revision;
        result.patch = kind == KIND_PATCH;

        if (kind == KIND_FULL) {
            List<Node> list = parseFull(buf);
            result.nodeCount = list.size();
            rebuild(list, revision);
        } else if (kind == KIND_PATCH) {
            List<PatchOp> ops = parsePatch(buf);
            result.nodeCount = views.size();
            result.dirtyCount = ops.size();
            dirtyCount += ops.size();
            applyOps(ops);
            appliedRevision = revision;
        } else {
            throw new IllegalStateException("ui-bridge: unknown kind " + kind);
        }

        buf.position(snapshotEnd);
        result.drawCommands = countDrawCommands(buf, drawBytes);
        result.root = root;
        appliedRevision = revision;
        Log.i(TAG, "materialized rev=" + revision + (result.patch ? " patch" : " full")
                + " nodes=" + result.nodeCount + " drawCmds=" + result.drawCommands
                + " dirty=" + dirtyCount);
        logLayoutStats();
        if (kind == KIND_FULL) scheduleHostSizeDump();
        if (root instanceof TextView) {
            TextView tv = (TextView) root;
            CharSequence t = tv.getText();
            Log.i(TAG, "root debug: class=TextView textLen=" + (t == null ? -1 : t.length())
                    + " sample=[" + (t == null ? "null" : t.toString().replace('\n', '|'))
                    + "] textSize=" + tv.getTextSize() + " vis=" + tv.getVisibility()
                    + " w=" + tv.getWidth() + " h=" + tv.getHeight());
        } else if (root != null) {
            Log.i(TAG, "root debug: class=" + root.getClass().getName());
        }
        return result;
    }

    /** 宿主布局后的可见尺寸诊断（guest 类名 -> 实际 w/h；定位遮挡/零尺寸）。 */
    private void scheduleHostSizeDump() {
        final View r = root;
        if (r == null) return;
        r.postDelayed(new Runnable() {
            @Override
            public void run() {
                StringBuilder sb = new StringBuilder();
                int shown = 0;
                for (Node n : nodes.values()) {
                    View v = views.get(n.objectId);
                    if (v == null || v.getVisibility() == View.GONE) continue;
                    if (v.getWidth() <= 0 || v.getHeight() <= 0) continue;
                    sb.append(" [").append(n.className).append(" ")
                            .append(v.getWidth()).append("x").append(v.getHeight()).append("]");
                    if (++shown >= 40) break;
                }
                Log.i(TAG, "host sizes=" + shown + sb);
                StringBuilder tree = new StringBuilder();
                dumpHostTree(root, tree, 0, new int[] {70});
                Log.i(TAG, "host tree" + tree);
                // 全屏覆盖层归因：面积 >=500x500 且有非零背景色的宿主 View。
                StringBuilder cover = new StringBuilder();
                for (View v : views.values()) {
                    if (v.getVisibility() != View.VISIBLE) continue;
                    if (v.getWidth() < 500 || v.getHeight() < 500) continue;
                    Drawable d = v.getBackground();
                    int c = d instanceof ColorDrawable ? ((ColorDrawable) d).getColor() : 0;
                    if (c == 0) continue;
                    String nodeCls = "?";
                    for (Node n : nodes.values()) {
                        if (views.get(n.objectId) == v) {
                            nodeCls = n.className;
                            break;
                        }
                    }
                    cover.append(" [").append(v.getClass().getSimpleName()).append("<")
                            .append(nodeCls).append("> ").append(v.getWidth()).append("x")
                            .append(v.getHeight()).append(" bg=#").append(Integer.toHexString(c))
                            .append(" a=").append(v.getAlpha()).append(" kids=")
                            .append(v instanceof ViewGroup ? ((ViewGroup) v).getChildCount() : -1)
                            .append("]");
                }
                Log.i(TAG, "full-bg views" + cover);
            }
        }, 800);
    }

    /** 宿主物化树 z-order 诊断：class/尺寸/可见性/alpha/背景色。 */
    private void dumpHostTree(View v, StringBuilder sb, int depth, int[] budget) {
        if (v == null || budget[0] <= 0) return;
        budget[0]--;
        int bg = 0;
        Drawable d = v.getBackground();
        if (d instanceof ColorDrawable) bg = ((ColorDrawable) d).getColor();
        for (int i = 0; i < depth; i++) sb.append("  ");
        sb.append(v.getClass().getSimpleName()).append(" ").append(v.getWidth()).append("x")
                .append(v.getHeight()).append(" vis=").append(v.getVisibility()).append(" a=")
                .append(v.getAlpha()).append(" bg=#").append(Integer.toHexString(bg)).append('\n');
        if (v instanceof ViewGroup) {
            ViewGroup g = (ViewGroup) v;
            for (int i = 0; i < g.getChildCount(); i++) {
                dumpHostTree(g.getChildAt(i), sb, depth + 1, budget);
            }
        }
    }

    /** 布局诊断：几何收敛度（bounds 有效 vs 交宿主布局的节点）。 */
    private void logLayoutStats() {
        int positive = 0;
        int zero = 0;
        StringBuilder sample = new StringBuilder();
        for (Node n : nodes.values()) {
            Prop b = n.prop(PROP_BOUNDS);
            boolean ok = b != null
                    && b.rect[2] - b.rect[0] > 0 && b.rect[3] - b.rect[1] > 0;
            if (ok) {
                positive++;
            } else {
                zero++;
                if (zero <= 6) {
                    Prop lp = n.prop(PROP_LAYOUT_PARAMS);
                    Prop m = n.prop(PROP_MEASURED);
                    sample.append(" [").append(n.className)
                            .append(" lp=").append(lp != null ? lp.rect[0] + "x" + lp.rect[1] : "?")
                            .append(" m=").append(m != null ? m.rect[0] + "x" + m.rect[1] : "?")
                            .append("]");
                }
            }
        }
        StringBuilder key = new StringBuilder();
        int keyCount = 0;
        for (Node n : nodes.values()) {
            String cls = n.className != null ? n.className : "";
            if (keyCount >= 10) break;
            if (!(cls.contains("BottomNav") || cls.contains("TabLayout")
                    || cls.contains("Toolbar") || cls.contains("Search"))) {
                continue;
            }
            Prop b = n.prop(PROP_BOUNDS);
            Prop lp = n.prop(PROP_LAYOUT_PARAMS);
            Prop m = n.prop(PROP_MEASURED);
            Prop o = n.prop(PROP_ORIENTATION);
            key.append(" [").append(cls)
                    .append(" b=").append(b != null
                            ? (b.rect[2] - b.rect[0]) + "x" + (b.rect[3] - b.rect[1]) : "?")
                    .append(" lp=").append(lp != null ? lp.rect[0] + "x" + lp.rect[1] : "?")
                    .append(" m=").append(m != null ? m.rect[0] + "x" + m.rect[1] : "?")
                    .append(" o=").append(o != null ? o.asInt() : "?")
                    .append("]");
            keyCount++;
        }
        Log.i(TAG, "layout bounds+=" + positive + " bounds0=" + zero + sample + key);
    }

    // ---- 解析（小端，镜像 ui_bridge_protocol.cc 线格式）----

    private List<Node> parseFull(ByteBuffer buf) {
        int count = buf.getInt();
        List<Node> list = new ArrayList<>(count);
        for (int i = 0; i < count; i++) {
            Node n = new Node();
            n.objectId = buf.getInt();
            n.handleKind = buf.get() & 0xFF;
            buf.get();
            buf.getShort();
            n.parentId = buf.getInt();
            n.childIndex = buf.getInt();
            n.guestViewId = buf.getInt();
            n.className = readStr16(buf);
            int props = buf.getShort() & 0xFFFF;
            for (int p = 0; p < props; p++) n.props.add(readProp(buf));
            list.add(n);
        }
        return list;
    }

    private List<PatchOp> parsePatch(ByteBuffer buf) {
        buf.getInt(); // base_revision（校验留给上层 revision 语义）
        int count = buf.getInt();
        List<PatchOp> ops = new ArrayList<>(count);
        for (int i = 0; i < count; i++) {
            PatchOp op = new PatchOp();
            op.op = buf.get() & 0xFF;
            buf.get();
            buf.getShort();
            op.objectId = buf.getInt();
            op.handleKind = buf.get() & 0xFF;
            buf.get();
            buf.getShort();
            op.parentId = buf.getInt();
            op.childIndex = buf.getInt();
            op.guestViewId = buf.getInt();
            op.className = readStr16(buf);
            int props = buf.getShort() & 0xFFFF;
            for (int p = 0; p < props; p++) op.props.add(readProp(buf));
            ops.add(op);
        }
        return ops;
    }

    private Prop readProp(ByteBuffer buf) {
        Prop p = new Prop();
        p.key = buf.getShort() & 0xFFFF;
        p.type = buf.get() & 0xFF;
        buf.get();
        switch (p.type) {
            case TYPE_INT:
            case TYPE_BOOL:
                p.i = buf.getInt();
                break;
            case TYPE_FLOAT:
                p.f = buf.getFloat();
                break;
            case TYPE_STRING:
                p.s = readStr16(buf);
                break;
            case TYPE_RECT:
                for (int i = 0; i < 4; i++) p.rect[i] = buf.getInt();
                break;
            case TYPE_POINT:
                p.rect[0] = buf.getInt();
                p.rect[1] = buf.getInt();
                break;
            case TYPE_HANDLE:
                p.i = buf.getInt();
                buf.get();
                buf.get();
                buf.getShort();
                break;
            case TYPE_BYTES: {
                int len = buf.getInt();
                if (len < 0 || len > buf.remaining()) {
                    throw new IllegalStateException("ui-bridge: bad bytes len " + len);
                }
                p.bytes = new byte[len];
                buf.get(p.bytes);
                break;
            }
            default:
                throw new IllegalStateException("ui-bridge: bad prop type " + p.type);
        }
        return p;
    }

    private static String readStr16(ByteBuffer buf) {
        int bytes = buf.getShort() & 0xFFFF;
        byte[] raw = new byte[bytes];
        buf.get(raw);
        return new String(raw, StandardCharsets.UTF_16LE);
    }

    private int countDrawCommands(ByteBuffer buf, int drawBytes) {
        int end = buf.position() + drawBytes;
        if (drawBytes == 0) return 0;
        int count = scanDrawSegment(buf);
        if (buf.position() != end) {
            throw new IllegalStateException("ui-bridge: draw segment drift");
        }
        return count;
    }

    // ---- 全量重建 ----

    private void rebuild(List<Node> list, int revision) {
        // 清旧树
        for (View v : views.values()) {
            if (v.getParent() instanceof ViewGroup) {
                ((ViewGroup) v.getParent()).removeView(v);
            }
        }
        views.clear();
        nodes.clear();
        absoluteViews.clear();
        childGravity.clear();
        childWeight.clear();
        appliedVideoPath.clear();
        dialogScrims.clear();
        overlayWrapper = null;
        root = null;

        Map<Integer, List<Node>> kids = new HashMap<>();
        List<Node> roots = new ArrayList<>();
        for (Node n : list) {
            nodes.put(n.objectId, n);
            n.children.clear();
        }
        for (Node n : list) {
            if (nodes.containsKey(n.parentId)) {
                List<Node> bucket = kids.get(n.parentId);
                if (bucket == null) {
                    bucket = new ArrayList<>();
                    kids.put(n.parentId, bucket);
                }
                bucket.add(n);
            } else {
                roots.add(n);
            }
        }
        for (Map.Entry<Integer, List<Node>> e : kids.entrySet()) {
            List<Node> bucket = e.getValue();
            bucket.sort((a, b) -> Integer.compare(a.childIndex, b.childIndex));
            for (Node c : bucket) nodes.get(e.getKey()).children.add(c.objectId);
        }
        roots.sort((a, b) -> Integer.compare(a.childIndex, b.childIndex));
        if (roots.isEmpty()) {
            throw new IllegalStateException("ui-bridge: snapshot has no root");
        }
        if (roots.size() == 1 && roots.get(0).prop(PROP_WINDOW_DIM) == null) {
            root = materializeTree(roots.get(0));
        } else {
            root = materializeMultiRoot(roots);
        }
        appliedRevision = revision;
    }

    private View materializeTree(Node node) {
        View v = materializeLeaf(node);
        views.put(node.objectId, v);
        for (int childId : node.children) {
            Node child = nodes.get(childId);
            if (child == null) continue;
            View cv = materializeTree(child);
            attach(v, cv, child, node);
        }
        return v;
    }

    /** 多根快照（Activity 根 + Dialog 窗口根）：wrapper 托管叠加层。 */
    private View materializeMultiRoot(List<Node> roots) {
        HostFrameLayout wrapper = new HostFrameLayout(context);
        overlayWrapper = wrapper;
        for (Node r : roots) {
            Prop dim = r.prop(PROP_WINDOW_DIM);
            if (dim != null && dim.f > 0f) {
                View scrim = makeScrim(dim.f);
                dialogScrims.put(r.objectId, scrim);
                wrapper.addView(scrim, new ViewGroup.MarginLayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.MATCH_PARENT));
            }
            View v = materializeTree(r);
            wrapper.addView(v, absoluteLayoutParams(r));
        }
        return wrapper;
    }

    /** Dialog dim 遮罩：可点击但无 guest 动作，只消费窗口外触摸（防穿透）。 */
    private View makeScrim(float dim) {
        View scrim = new View(context);
        int alpha = Math.max(0, Math.min(255, Math.round(dim * 255f)));
        scrim.setBackgroundColor(alpha << 24);
        scrim.setClickable(true);
        scrim.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { }
        });
        return scrim;
    }

    private ViewGroup.MarginLayoutParams absoluteLayoutParams(Node node) {
        Prop b = node.prop(PROP_BOUNDS);
        if (b != null) {
            int w = b.rect[2] - b.rect[0];
            int h = b.rect[3] - b.rect[1];
            if (w > 0 && h > 0) {
                ViewGroup.MarginLayoutParams lp = new ViewGroup.MarginLayoutParams(w, h);
                lp.leftMargin = b.rect[0];
                lp.topMargin = b.rect[1];
                return lp;
            }
        }
        return new ViewGroup.MarginLayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT);
    }

    /** 窗口根（无父节点）的增量出现：把当前根移入 wrapper 并返回。 */
    private HostFrameLayout ensureOverlayWrapper() {
        if (overlayWrapper != null) return overlayWrapper;
        HostFrameLayout wrapper = new HostFrameLayout(context);
        if (root != null) {
            if (root.getParent() instanceof ViewGroup) {
                ((ViewGroup) root.getParent()).removeView(root);
            }
            // 旧根曾按 540x960 逻辑窗口由 MainActivity 缩放铺满内容区；移入
            // overlay wrapper 后 wrapper 自身才是缩放面，旧根必须回到未缩放态，
            // 否则宿主缩放与旧根缩放叠加（弹 Dialog 后整屏放大 ~1.44x 的根因）。
            root.setScaleX(1f);
            root.setScaleY(1f);
            root.setTranslationX(0f);
            root.setTranslationY(0f);
            wrapper.addView(root, new ViewGroup.MarginLayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT));
        }
        overlayWrapper = wrapper;
        root = wrapper;
        return wrapper;
    }

    private View materializeLeaf(Node node) {
        // 按类名后缀匹配宿主控件：知乎大量使用自定义子类（ZHImageView、
        // ZTextView、AppCompatTextView…），精确匹配会全部退化成通用容器，
        // 图片/文字等属性无法物化（开屏/骨架因此整片空白）。
        String cls = node.className != null ? node.className : "";
        View v;
        if (cls.endsWith("EditText")) {
            v = new EditText(context);
        } else if (cls.endsWith("CheckBox")) {
            v = new CheckBox(context);
        } else if (cls.endsWith("RadioButton")) {
            v = new RadioButton(context);
        } else if (cls.endsWith("ToggleButton")) {
            v = new ToggleButton(context);
        } else if (cls.endsWith("SeekBar")) {
            v = new SeekBar(context);
        } else if (cls.endsWith("ImageButton")) {
            // ImageButton 以图片出画（AOSP ImageButton extends ImageView）。
            v = new ImageView(context);
        } else if (cls.endsWith("Button") || cls.contains("Button")) {
            v = new Button(context);
        } else if (cls.endsWith("TextSwitcher") || cls.endsWith("ViewSwitcher") ||
                   cls.endsWith("ViewAnimator")) {
            // 搜索栏提示轮播（SearchTextSwitcher 等）：快照文本挂在切换器节点上，
            // 容器物化不会出字；按文本叶节点出画（轮播态取当前文本）。
            v = new TextView(context);
        } else if (cls.endsWith("Switch") || cls.endsWith("SwitchCompat")) {
            // 仅精确开关类（TextSwitcher/ViewSwitcher 含 "Switch" 子串——搜索栏
            // 的提示轮播被误物化成 Switch 的根因）。
            v = new Switch(context);
        } else if (cls.endsWith("TextView")) {
            v = new TextView(context);
        } else if (cls.endsWith("ImageView") || cls.contains("DraweeView")) {
            // Fresco Drawee 族最终继承 ImageView；按图片叶节点物化。
            v = new ImageView(context);
        } else if (cls.contains("ProgressBar")) {
            v = new ProgressBar(context, null, android.R.attr.progressBarStyleHorizontal);
        } else if (cls.endsWith("VideoView")) {
            v = new VideoView(context);
        } else if (cls.endsWith("HorizontalScrollView")) {
            // 真滚动容器（此前按 FrameLayout 物化，长内容不可达/不可滚）。
            v = new HorizontalScrollView(context);
        } else if (cls.endsWith("ScrollView")) {
            final ScrollView scrollView = new ScrollView(context);
            // 首次布局后回内容顶部：宿主焦点/search 与子项分批挂载可能把
            // ScrollView 带到中部。只校正一次，不与用户后续滑动对抗。
            scrollView.addOnLayoutChangeListener(new View.OnLayoutChangeListener() {
                private boolean corrected;

                @Override
                public void onLayoutChange(View v, int l, int t, int r, int b,
                                           int ol, int ot, int or, int ob) {
                    if (corrected) return;
                    corrected = true;
                    scrollView.scrollTo(0, 0);
                }
            });
            v = scrollView;
        } else if (isLinearContainer(node, cls)) {
            // AOSP 线性容器（含 ZHLinearLayout2 等派生类）：按 orientation
            // 做行/列流式布局。
            v = new HostLinearLayout(context);
        } else {
            // FrameLayout / RelativeLayout / ScrollView / 自定义容器：
            // 宿主 FrameLayout 语义（子 View 按 gravity/margin 叠放）。
            v = new HostFrameLayout(context);
        }
        applyProps(v, node);
        wireClick(v, node);
        return v;
    }

    /** 容器是否为 LinearLayout 族：guest 父类链判定优先，类名兜底。 */
    private static boolean isLinearContainer(Node node, String cls) {
        Prop lk = node.prop(PROP_LAYOUT_KIND);
        if (lk != null && lk.asInt() != 0) return lk.asInt() == 1;
        return cls.contains("LinearLayout");
    }

    private void attach(View parent, View child, Node node, Node parentNode) {
        if (!(parent instanceof ViewGroup)) {
            Log.w(TAG, "attach skipped: parent not a group (" + node.className + ")");
            return;
        }
        ViewGroup group = (ViewGroup) parent;
        // 滚动容器：ScrollView 只接受单子项且忽略绝对 margin 摆放——内容子项
        // 按滚动内容自量（高度 WRAP_CONTENT），滚动由宿主真 ScrollView 承担。
        if (group instanceof ScrollView || group instanceof HorizontalScrollView) {
            if (group.getChildCount() > 0 && group.getChildAt(0) != child) {
                group.removeAllViews();
            }
            group.addView(child, group instanceof HorizontalScrollView
                    ? new HorizontalScrollView.LayoutParams(
                            ViewGroup.LayoutParams.WRAP_CONTENT,
                            ViewGroup.LayoutParams.MATCH_PARENT)
                    : new ScrollView.LayoutParams(
                            ViewGroup.LayoutParams.MATCH_PARENT,
                            ViewGroup.LayoutParams.WRAP_CONTENT));
            return;
        }
        Prop bounds = node.prop(PROP_BOUNDS);
        int w = 0;
        int h = 0;
        int left = 0;
        int top = 0;
        if (bounds != null) {
            left = bounds.rect[0];
            top = bounds.rect[1];
            w = bounds.rect[2] - bounds.rect[0];
            h = bounds.rect[3] - bounds.rect[1];
        }
        boolean absolute = w > 0 && h > 0;
        // 文本节点被 guest 压成 <8px 高（自定义 Tab/搜索栏的中间态）时，绝对
        // 几何会把文字裁成一条线；改交宿主按文本自量（wrap_content）。
        if (absolute && h < 8 && child instanceof TextView) absolute = false;
        ViewGroup.MarginLayoutParams lp;
        if (absolute) {
            // guest bounds 为父容器相对坐标（AOSP View.mLeft/mTop 口径）：
            // 直接作为宿主的 left/top margin，不再减父原点。
            lp = new ViewGroup.MarginLayoutParams(w, h);
            lp.leftMargin = left;
            lp.topMargin = top;
            absoluteViews.put(child, Boolean.TRUE);
        } else {
            // traversal 未收敛（节点几何为空）：布局归宿主。按 guest
            // LayoutParams 的宽高意图（MATCH_PARENT / WRAP_CONTENT / 定值）
            // 交宿主真 View 自行 measure；位置由宿主容器顺序摆放。
            Prop glp = node.prop(PROP_LAYOUT_PARAMS);
            Prop measured = node.prop(PROP_MEASURED);
            Prop margins = node.prop(PROP_LAYOUT_MARGINS);
            int gw = glp != null ? glp.rect[0] : 0;
            int gh = glp != null ? glp.rect[1] : 0;
            int mw = measured != null ? measured.rect[0] : 0;
            int mh = measured != null ? measured.rect[1] : 0;
            Prop weight = node.prop(PROP_LAYOUT_WEIGHT);
            float wv = weight != null ? weight.f : 0f;
            boolean weightedLinear = wv > 0 && parent instanceof HostLinearLayout;
            if (weightedLinear) childWeight.put(child, wv);
            else childWeight.remove(child);
            // guest 已测出 0x0 的 match_parent 子项（如 CoordinatorLayout 的
            // ScrollingViewBehavior 在当前 AppBar 高度下给出 0 高）：保留 guest
            // 测量结果，不再交宿主按 match_parent 撑满（否则滚动内容白底覆盖
            // 整个 AppBar，首帧只剩白屏）。
            boolean measuredCollapsedW =
                    mw == 0 && gw == ViewGroup.LayoutParams.MATCH_PARENT;
            boolean measuredCollapsedH =
                    mh == 0 && gh == ViewGroup.LayoutParams.MATCH_PARENT;
            // 0dp + weight 是"剩余空间分配"占位（AOSP LinearLayout）：尺寸 0
            // 保留到宿主 measure 的权重轮再展开，不能当 WRAP_CONTENT。
            int lw = measuredCollapsedW ? 0
                    : weightedLinear && gw == 0 ? 0 : lpDimension(gw, mw);
            int lh = measuredCollapsedH ? 0
                    : weightedLinear && gh == 0 ? 0 : lpDimension(gh, mh);
            lp = new ViewGroup.MarginLayoutParams(lw, lh);
            if (margins != null) {
                lp.leftMargin = Math.max(margins.rect[0], 0);
                lp.topMargin = Math.max(margins.rect[1], 0);
                lp.rightMargin = Math.max(margins.rect[2], 0);
                lp.bottomMargin = Math.max(margins.rect[3], 0);
            }
            Prop grav = node.prop(PROP_LAYOUT_GRAVITY);
            if (grav != null) childGravity.put(child, grav.asInt());
            absoluteViews.remove(child);
        }
        group.addView(child, lp);
    }

    /** guest LayoutParams 宽高 -> 宿主布局约束。
     *  WRAP_CONTENT 有已测尺寸时优先用已测值（traversal 中断前的有效测量），
     *  否则交宿主按内容自量。 */
    private static int lpDimension(int v, int measured) {
        // measured 不作为 fallback 尺寸：traversal 中断前的测量可能带着极端
        // 约束值（实测 TextView mMeasuredWidth=49152），宿主直接采用会撑爆
        // 布局。WRAP_CONTENT 交宿主按内容自量最稳。
        if (v > 0) return v;
        if (v == ViewGroup.LayoutParams.MATCH_PARENT) {
            return ViewGroup.LayoutParams.MATCH_PARENT;
        }
        return ViewGroup.LayoutParams.WRAP_CONTENT;
    }

    private void applyProps(View v, Node node) {
        if (v instanceof TextView) {
            TextView tv = (TextView) v;
            Prop text = node.prop(PROP_TEXT);
            if (text != null && text.s != null) {
                // 快照回写文本：抑制 TextWatcher，避免 host->guest 回声
                // （用户键入仍正常回灌）。
                suppressTextEvents.add(tv);
                tv.setText(text.s);
                suppressTextEvents.remove(tv);
            }
            Prop ts = node.prop(PROP_TEXT_SIZE);
            if (ts != null && ts.f > 0) {
                tv.setTextSize(TypedValue.COMPLEX_UNIT_PX, ts.f);
            }
            // 0 = guest 未显式设色（AOSP 落主题色）；不要用 0 覆盖宿主默认，
            // 否则文字透明不可见。
            Prop tc = node.prop(PROP_TEXT_COLOR);
            if (tc != null && tc.asInt() != 0) tv.setTextColor(tc.asInt());
            Prop gv = node.prop(PROP_GRAVITY);
            if (gv != null && gv.asInt() != 0) tv.setGravity(gv.asInt());
        }
        if (v instanceof ImageView) {
            Prop img = node.prop(PROP_IMAGE_BYTES);
            Bitmap bmp = null;
            if (img != null && img.bytes != null && img.bytes.length > 0) {
                bmp = BitmapFactory.decodeByteArray(img.bytes, 0, img.bytes.length);
            }
            if (bmp != null) {
                ((ImageView) v).setImageDrawable(new BitmapDrawable(context.getResources(), bmp));
                ((ImageView) v).setScaleType(ImageView.ScaleType.FIT_CENTER);
            } else {
                // XML vector / 非位图 drawable：按 guest resId 用 guest APK 的
                // 真实资源表装载宿主 Drawable（图标/层列表等通用出画）。
                Prop ir = node.prop(PROP_IMAGE_RESOURCE);
                int resId = ir != null ? ir.asInt() : 0;
                if (resId != 0 && drawableLoader != null) {
                    Drawable d = drawableLoader.load(resId);
                    if (d != null) {
                        ((ImageView) v).setImageDrawable(d);
                        ((ImageView) v).setScaleType(ImageView.ScaleType.FIT_CENTER);
                    }
                }
            }
        }
        Prop bg = node.prop(PROP_BG_COLOR);
        if (bg != null && bg.asInt() != 0) {
            // 0 = guest 未设背景（AOSP no background）：保留宿主控件默认背景
            // （Button/EditText 的原生外观），不被透明色抹掉。
            v.setBackgroundColor(bg.asInt());
        }
        if (v instanceof ProgressBar) {
            Prop progress = node.prop(PROP_PROGRESS);
            if (progress != null) ((ProgressBar) v).setProgress(progress.asInt());
        }
        if (v instanceof CompoundButton) {
            // Switch/CheckBox/RadioButton/ToggleButton 选中态（guest mChecked）
            Prop checked = node.prop(PROP_CHECKED);
            if (checked != null) {
                ((CompoundButton) v).setChecked(checked.asInt() != 0);
            }
        }
        if (v instanceof VideoView) {
            applyVideoProps((VideoView) v, node);
        }
        if (v instanceof HostLinearLayout) {
            Prop g = node.prop(PROP_GRAVITY);
            if (g != null) ((HostLinearLayout) v).gravityProp = g.asInt();
            Prop o = node.prop(PROP_ORIENTATION);
            if (o != null) ((HostLinearLayout) v).orientationProp = o.asInt();
        }
        Prop pad = node.prop(PROP_PADDING);
        if (pad != null) {
            v.setPadding(pad.rect[0], pad.rect[1], pad.rect[2], pad.rect[3]);
        }
        Prop vis = node.prop(PROP_VISIBILITY);
        if (vis != null) v.setVisibility(vis.asInt());
        Prop alpha = node.prop(PROP_ALPHA);
        // alpha≈0 通常来自入场动画起始帧；宿主不跑 guest 动画器，直接采用 0
        // 会让整块内容永久透明（实测首帧只剩窗口背景灰块）。基础 UI 口径：
        // 0 视作"未开始动画"，按可见物化；非零沿用 guest 值。
        if (alpha != null && alpha.f > 0.01f) {
            v.setAlpha(Math.max(0f, Math.min(alpha.f, 1f)));
        }
        Prop tr = node.prop(PROP_TRANSLATION);
        if (tr != null) {
            v.setTranslationX(tr.rect[0]);
            v.setTranslationY(tr.rect[1]);
        }
    }

    // ---- VideoView 桥接（guest setVideoPath/seekTo/start -> 宿主真播放）----

    private void applyVideoProps(VideoView vv, Node node) {
        Prop path = node.prop(PROP_VIDEO_PATH);
        if (path != null && path.s != null && !path.s.isEmpty()) {
            String prev = appliedVideoPath.get(node.objectId);
            if (!path.s.equals(prev)) {
                String resolved = resolveVideoPath(path.s);
                if (resolved != null) {
                    vv.setVideoPath(resolved);
                    appliedVideoPath.put(node.objectId, path.s);
                }
            }
        }
        Prop seek = node.prop(PROP_VIDEO_SEEK);
        if (seek != null && seek.asInt() > 0) vv.seekTo(seek.asInt());
        Prop playing = node.prop(PROP_VIDEO_PLAYING);
        if (playing != null) {
            if (playing.asInt() != 0) vv.start();
            else vv.pause();
        }
    }

    /** android.resource://pkg/type/name -> APK 内 res/type/name.* 抽到缓存文件；
     *  网络/普通路径原样返回。 */
    private String resolveVideoPath(String src) {
        if (src.startsWith("android.resource://")) {
            String rest = src.substring("android.resource://".length());
            String[] parts = rest.split("/");
            if (parts.length < 3) return null;
            String entryPrefix = "res/" + parts[1] + "/" + parts[2];
            File out = new File(context.getCacheDir(), "bridge-media/" + parts[2] + ".bin");
            if (out.exists() && out.length() > 0) return out.getAbsolutePath();
            if (apkPath == null) return null;
            try (ZipFile zip = new ZipFile(apkPath)) {
                Enumeration<? extends ZipEntry> en = zip.entries();
                while (en.hasMoreElements()) {
                    ZipEntry ze = en.nextElement();
                    String zn = ze.getName();
                    if (!zn.equals(entryPrefix) && !zn.startsWith(entryPrefix + ".")) continue;
                    File parent = out.getParentFile();
                    if (parent != null) parent.mkdirs();
                    try (InputStream in = zip.getInputStream(ze);
                         FileOutputStream fo = new FileOutputStream(out)) {
                        byte[] buf = new byte[64 * 1024];
                        int n;
                        while ((n = in.read(buf)) > 0) fo.write(buf, 0, n);
                    }
                    return out.getAbsolutePath();
                }
            } catch (Throwable t) {
                Log.w(TAG, "video extract failed: " + src, t);
            }
            return null;
        }
        return src;
    }

    private void wireClick(final View v, final Node node) {
        final int objectId = node.objectId;
        // 交互回灌面（与 click 独立）：文本键入 / SeekBar 拖拽 / 长按。
        wireEditTextBackflow(v, objectId);
        wireSeekBackflow(v, objectId);
        wireLongClick(v, node, objectId);
        Prop hasClick = node.prop(PROP_HAS_CLICK);
        if (hasClick == null || hasClick.asInt() == 0) {
            // AOSP 命中语义：没有 click listener 的 View 不消费触摸。此前对
            // 每个物化 View 都 setClickable(true)，导致全屏透明 overlay_container
            // 与行内 TextView 抢走点击并注入错误节点（重试/底部导航全失效）。
            v.setOnClickListener(null);
            v.setClickable(false);
            return;
        }
        v.setClickable(true);
        v.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View vv) {
                String status;
                try {
                    // 按快照 handle.object_id 回灌：mId 为 0 的视图（tab 容器/
                    // 自绘行）在 viewId 通道会撞到任意无 id 记录，派发到已回收
                    // 监听器（dirty reference InternalError）。
                    status = NativeBridge.nativeDispatchViewObjectClick(objectId);
                } catch (Throwable t) {
                    status = "error: " + t;
                }
                Log.i(TAG, "host click objectId=" + objectId + " -> " + status);
            }
        });
    }

    /** EditText 键入回灌：宿主文本变化 -> guest setText（内建再通知 TextWatcher）。 */
    private void wireEditTextBackflow(final View v, final int objectId) {
        if (!(v instanceof EditText)) return;
        if (!textWatchersInstalled.add(v)) return;
        ((EditText) v).addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {
            }

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {
            }

            @Override
            public void afterTextChanged(Editable s) {
                if (suppressTextEvents.contains(v)) return;
                String text = s != null ? s.toString() : "";
                String status;
                try {
                    status = NativeBridge.nativeDispatchViewText(objectId, text);
                } catch (Throwable t) {
                    status = "error: " + t;
                }
                Log.i(TAG, "host text objectId=" + objectId + " -> " + status);
            }
        });
    }

    /** SeekBar 拖拽回灌：fromUser 进度/起止跟踪 -> guest OnSeekBarChangeListener。 */
    private void wireSeekBackflow(final View v, final int objectId) {
        if (!(v instanceof SeekBar)) return;
        if (!seekListenersInstalled.add(v)) return;
        ((SeekBar) v).setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar bar, int progress, boolean fromUser) {
                if (!fromUser) return;  // 快照回写（setProgress）不回灌
                dispatchSeek(objectId, progress, 0);
            }

            @Override
            public void onStartTrackingTouch(SeekBar bar) {
                dispatchSeek(objectId, bar.getProgress(), 1);
            }

            @Override
            public void onStopTrackingTouch(SeekBar bar) {
                dispatchSeek(objectId, bar.getProgress(), 2);
            }
        });
    }

    private void dispatchSeek(int objectId, int progress, int phase) {
        String status;
        try {
            status = NativeBridge.nativeDispatchViewSeek(objectId, progress, phase);
        } catch (Throwable t) {
            status = "error: " + t;
        }
        Log.i(TAG, "host seek objectId=" + objectId + " phase=" + phase
                + " value=" + progress + " -> " + status);
    }

    /** 长按回灌：仅在 guest 登记了 OnLongClickListener 时安装（避免吞手势）。 */
    private void wireLongClick(final View v, final Node node, final int objectId) {
        Prop hasLong = node.prop(PROP_HAS_LONG_CLICK);
        boolean wanted = hasLong != null && hasLong.asInt() != 0;
        if (!wanted) {
            if (longClickInstalled.remove(v)) v.setOnLongClickListener(null);
            return;
        }
        if (!longClickInstalled.add(v)) return;
        v.setOnLongClickListener(new View.OnLongClickListener() {
            @Override
            public boolean onLongClick(View vv) {
                String status;
                try {
                    status = NativeBridge.nativeDispatchViewLongClick(objectId);
                } catch (Throwable t) {
                    status = "error: " + t;
                }
                Log.i(TAG, "host long-click objectId=" + objectId + " -> " + status);
                return true;
            }
        });
    }

    // ---- 增量 patch ----

    private void applyOps(List<PatchOp> ops) {
        for (PatchOp op : ops) {
            switch (op.op) {
                case PATCH_ADD: {
                    // 同一 objectId 重加（快照/patch 交叠）：先摘掉旧视图，
                    // 否则旧实例留在父容器里与新实例重叠（位置/点击错乱根因）。
                    View prev = views.get(op.objectId);
                    if (prev != null) {
                        if (prev.getParent() instanceof ViewGroup) {
                            ((ViewGroup) prev.getParent()).removeView(prev);
                        }
                    }
                    Node node = new Node();
                    node.objectId = op.objectId;
                    node.handleKind = op.handleKind;
                    node.parentId = op.parentId;
                    node.childIndex = op.childIndex;
                    node.guestViewId = op.guestViewId;
                    node.className = op.className;
                    node.props.addAll(op.props);
                    nodes.put(node.objectId, node);
                    View v = materializeLeaf(node);
                    views.put(node.objectId, v);
                    View parent = views.get(node.parentId);
                    Node parentNode = nodes.get(node.parentId);
                    if (parent != null) {
                        attach(parent, v, node, parentNode);
                    } else if (!nodes.containsKey(node.parentId)) {
                        // 窗口根（parent_id = kNoObject = -1）：Dialog 内容树
                        // 作为叠加根挂到 wrapper（宿主不知道 Dialog 对象）。
                        HostFrameLayout wrapper = ensureOverlayWrapper();
                        Prop dim = node.prop(PROP_WINDOW_DIM);
                        if (dim != null && dim.f > 0f) {
                            View scrim = makeScrim(dim.f);
                            dialogScrims.put(node.objectId, scrim);
                            wrapper.addView(scrim, new ViewGroup.MarginLayoutParams(
                                    ViewGroup.LayoutParams.MATCH_PARENT,
                                    ViewGroup.LayoutParams.MATCH_PARENT));
                        }
                        wrapper.addView(v, absoluteLayoutParams(node));
                        Prop rb = node.prop(PROP_BOUNDS);
                        Prop rd = node.prop(PROP_WINDOW_DIM);
                        Log.i(TAG, "patch add root id=" + node.objectId + " cls="
                                + node.className + " bounds="
                                + (rb != null ? rb.rect[0] + "," + rb.rect[1] + ","
                                        + rb.rect[2] + "," + rb.rect[3] : "none")
                                + " dim=" + (rd != null ? rd.f : -1f)
                                + " wrapperKids=" + wrapper.getChildCount()
                                + " v=" + v.getClass().getSimpleName());
                    }
                    break;
                }
                case PATCH_REMOVE: {
                    View v = views.remove(op.objectId);
                    nodes.remove(op.objectId);
                    if (v != null && v.getParent() instanceof ViewGroup) {
                        ((ViewGroup) v.getParent()).removeView(v);
                    }
                    View scrim = dialogScrims.remove(op.objectId);
                    if (scrim != null && scrim.getParent() instanceof ViewGroup) {
                        ((ViewGroup) scrim.getParent()).removeView(scrim);
                    }
                    if (v == root) root = null;
                    break;
                }
                case PATCH_UPDATE: {
                    Node node = nodes.get(op.objectId);
                    View v = views.get(op.objectId);
                    if (node == null || v == null) {
                        Log.w(TAG, "update on unknown node " + op.objectId);
                        break;
                    }
                    for (Prop p : op.props) {
                        boolean replaced = false;
                        for (Prop have : node.props) {
                            if (have.key == p.key) {
                                have.type = p.type;
                                have.i = p.i;
                                have.f = p.f;
                                have.s = p.s;
                                System.arraycopy(p.rect, 0, have.rect, 0, 4);
                                replaced = true;
                                break;
                            }
                        }
                        if (!replaced) node.props.add(p);
                    }
                    if (op.guestViewId != -1) node.guestViewId = op.guestViewId;
                    applyProps(v, node);
                    wireClick(v, node);
                    // bounds 变更：有几何 -> guest 绝对布局；无几何 -> 宿主布局。
                    Prop bounds = node.prop(PROP_BOUNDS);
                    if (bounds != null && v.getLayoutParams() instanceof ViewGroup.MarginLayoutParams) {
                        ViewGroup.MarginLayoutParams lp =
                                (ViewGroup.MarginLayoutParams) v.getLayoutParams();
                        int w = bounds.rect[2] - bounds.rect[0];
                        int h = bounds.rect[3] - bounds.rect[1];
                        if (w > 0 && h > 0) {
                            // 父容器相对坐标：bounds.left/top 即宿主 margin。
                            lp.leftMargin = bounds.rect[0];
                            lp.topMargin = bounds.rect[1];
                            lp.width = w;
                            lp.height = h;
                            absoluteViews.put(v, Boolean.TRUE);
                        } else {
                            Prop glp = node.prop(PROP_LAYOUT_PARAMS);
                            Prop measured = node.prop(PROP_MEASURED);
                            Prop margins = node.prop(PROP_LAYOUT_MARGINS);
                            int gw = glp != null ? glp.rect[0] : 0;
                            int gh = glp != null ? glp.rect[1] : 0;
                            int mw = measured != null ? measured.rect[0] : 0;
                            int mh = measured != null ? measured.rect[1] : 0;
                            Prop weight = node.prop(PROP_LAYOUT_WEIGHT);
                            float wv = weight != null ? weight.f : 0f;
                            boolean weightedLinear = wv > 0
                                    && v.getParent() instanceof HostLinearLayout;
                            if (weightedLinear) childWeight.put(v, wv);
                            else childWeight.remove(v);
                            lp.width = weightedLinear && gw == 0 ? 0 : lpDimension(gw, mw);
                            lp.height = weightedLinear && gh == 0 ? 0 : lpDimension(gh, mh);
                            if (margins != null) {
                                lp.leftMargin = Math.max(margins.rect[0], 0);
                                lp.topMargin = Math.max(margins.rect[1], 0);
                                lp.rightMargin = Math.max(margins.rect[2], 0);
                                lp.bottomMargin = Math.max(margins.rect[3], 0);
                            } else {
                                lp.leftMargin = 0;
                                lp.topMargin = 0;
                            }
                            Prop grav = node.prop(PROP_LAYOUT_GRAVITY);
                            if (grav != null) childGravity.put(v, grav.asInt());
                            absoluteViews.remove(v);
                        }
                        v.setLayoutParams(lp);
                    }
                    break;
                }
                default:
                    Log.w(TAG, "unknown patch op " + op.op);
                    break;
            }
        }
    }

    // ---- 物化容器 ----
    //
    // 双轨布局：
    // 1) guest traversal 已产出绝对 bounds 的节点：按 guest 几何精确摆放
    //    （不做二次解释，保真度最高）；
    // 2) bounds 缺失（traversal 因个别组件失败未收敛）：按 guest
    //    LayoutParams（MATCH_PARENT / WRAP_CONTENT / 定值）构造宿主 MeasureSpec，
    //    由宿主真 View 自己 measure/layout。这就是"视图层桥接后布局归宿主"。

    private static final int LAYOUT_FRAME = 0;
    private static final int LAYOUT_VERTICAL = 1;
    private static final int LAYOUT_HORIZONTAL = 2;

    /** 子 View measure；返回内容包围盒 (w,h)。 */
    private int[] measureChildrenHost(ViewGroup group, int ws, int hs, int mode) {
        int parentW = View.MeasureSpec.getSize(ws);
        int parentH = View.MeasureSpec.getSize(hs);
        int contentW = Math.max(0, parentW - group.getPaddingLeft() - group.getPaddingRight());
        int contentH = Math.max(0, parentH - group.getPaddingTop() - group.getPaddingBottom());
        int maxW = 0;
        int maxH = 0;
        int totalW = 0;
        int totalH = 0;
        ArrayList<View> weighted = new ArrayList<>();
        float totalWeight = 0f;
        for (int i = 0; i < group.getChildCount(); i++) {
            View child = group.getChildAt(i);
            if (child.getVisibility() == View.GONE) continue;
            ViewGroup.MarginLayoutParams lp =
                    (ViewGroup.MarginLayoutParams) child.getLayoutParams();
            int hMargin = lp.leftMargin + lp.rightMargin;
            int vMargin = lp.topMargin + lp.bottomMargin;
            boolean absolute = absoluteViews.containsKey(child)
                    && lp.width > 0 && lp.height > 0;
            int cw;
            int ch;
            if (absolute) {
                child.measure(View.MeasureSpec.makeMeasureSpec(lp.width, View.MeasureSpec.EXACTLY),
                        View.MeasureSpec.makeMeasureSpec(lp.height, View.MeasureSpec.EXACTLY));
                cw = lp.leftMargin + child.getMeasuredWidth() + lp.rightMargin;
                ch = lp.topMargin + child.getMeasuredHeight() + lp.bottomMargin;
                maxW = Math.max(maxW, cw);
                maxH = Math.max(maxH, ch);
                if (mode == LAYOUT_VERTICAL) totalH = Math.max(totalH, ch);
                if (mode == LAYOUT_HORIZONTAL) totalW = Math.max(totalW, cw);
            } else {
                child.measure(makeChildSpec(contentW, ws, lp.width),
                        makeChildSpec(contentH, hs, lp.height));
                cw = child.getMeasuredWidth() + hMargin;
                ch = child.getMeasuredHeight() + vMargin;
                maxW = Math.max(maxW, cw);
                maxH = Math.max(maxH, ch);
                if (mode == LAYOUT_VERTICAL) totalH += ch;
                else if (mode == LAYOUT_HORIZONTAL) totalW += cw;
                Float weight = childWeight.get(child);
                if (weight != null && weight > 0) {
                    weighted.add(child);
                    totalWeight += weight;
                }
            }
        }
        // AOSP LinearLayout 权重轮：0dp/weight 子项分配剩余空间。
        if ((mode == LAYOUT_VERTICAL || mode == LAYOUT_HORIZONTAL) && totalWeight > 0) {
            int available = mode == LAYOUT_VERTICAL ? contentH : contentW;
            int used = mode == LAYOUT_VERTICAL ? totalH : totalW;
            int extra = available - used;
            if (extra > 0) {
                for (View child : weighted) {
                    Float f = childWeight.get(child);
                    if (f == null || f <= 0) continue;
                    int share = (int) (extra * f / totalWeight);
                    int nw = child.getMeasuredWidth();
                    int nh = child.getMeasuredHeight();
                    if (mode == LAYOUT_VERTICAL) nh += share;
                    else nw += share;
                    child.measure(View.MeasureSpec.makeMeasureSpec(nw, View.MeasureSpec.EXACTLY),
                            View.MeasureSpec.makeMeasureSpec(nh, View.MeasureSpec.EXACTLY));
                }
                if (mode == LAYOUT_VERTICAL) totalH = available;
                else totalW = available;
            }
        }
        int extW = mode == LAYOUT_HORIZONTAL ? totalW : maxW;
        int extH = mode == LAYOUT_VERTICAL ? totalH : maxH;
        return new int[] {
                Math.max(extW + group.getPaddingLeft() + group.getPaddingRight(), 0),
                Math.max(extH + group.getPaddingTop() + group.getPaddingBottom(), 0)};
    }

    /** 宿主 measure 语义：定值 -> EXACTLY；MATCH_PARENT/WRAP_CONTENT 按父 spec。 */
    private static int makeChildSpec(int contentSize, int parentSpec, int childDimension) {
        int parentMode = View.MeasureSpec.getMode(parentSpec);
        if (childDimension >= 0) {
            int dim = childDimension;
            // guest LP 解析异常值兜底（实测某分隔 View layout_width 落成
            // 49152px，会把同一行后续 tab 全部推到屏外）；只夹极端值，
            // 正常固定尺寸/滚动内容不受影响。
            if (dim > 16384 && contentSize > 0) dim = contentSize;
            return View.MeasureSpec.makeMeasureSpec(dim, View.MeasureSpec.EXACTLY);
        }
        if (childDimension == ViewGroup.LayoutParams.MATCH_PARENT) {
            return View.MeasureSpec.makeMeasureSpec(contentSize,
                    parentMode == View.MeasureSpec.UNSPECIFIED
                            ? View.MeasureSpec.UNSPECIFIED : View.MeasureSpec.EXACTLY);
        }
        return View.MeasureSpec.makeMeasureSpec(contentSize,
                parentMode == View.MeasureSpec.UNSPECIFIED
                        ? View.MeasureSpec.UNSPECIFIED : View.MeasureSpec.AT_MOST);
    }

    /** 宿主布局：绝对几何按 guest 位置；无几何节点按容器语义摆放。 */
    private void layoutChildrenHost(ViewGroup group, int mode, int gravityProp) {
        int groupWidth = group.getWidth();
        int groupHeight = group.getHeight();
        int contentW = Math.max(0,
                groupWidth - group.getPaddingLeft() - group.getPaddingRight());
        int contentH = Math.max(0,
                groupHeight - group.getPaddingTop() - group.getPaddingBottom());
        int x = group.getPaddingLeft();
        int y = group.getPaddingTop();
        for (int i = 0; i < group.getChildCount(); i++) {
            View child = group.getChildAt(i);
            if (child.getVisibility() == View.GONE) continue;
            ViewGroup.MarginLayoutParams lp =
                    (ViewGroup.MarginLayoutParams) child.getLayoutParams();
            int cw = child.getMeasuredWidth();
            int ch = child.getMeasuredHeight();
            int left;
            int top;
            boolean absolute = absoluteViews.containsKey(child)
                    && lp.width > 0 && lp.height > 0;
            if (absolute) {
                left = lp.leftMargin;
                top = lp.topMargin;
                x = Math.max(x, left + cw + lp.rightMargin);
                y = Math.max(y, top + ch + lp.bottomMargin);
            } else if (mode == LAYOUT_VERTICAL) {
                left = group.getPaddingLeft() + lp.leftMargin;
                if ((gravityProp & 0x1) != 0) {  // Gravity.CENTER_HORIZONTAL
                    left = Math.max(group.getPaddingLeft(),
                            group.getPaddingLeft() + (contentW - cw) / 2);
                }
                top = y + lp.topMargin;
                y = top + ch + lp.bottomMargin;
            } else if (mode == LAYOUT_HORIZONTAL) {
                top = group.getPaddingTop() + lp.topMargin;
                if ((gravityProp & 0x10) != 0) {  // Gravity.CENTER_VERTICAL
                    top = Math.max(group.getPaddingTop(),
                            group.getPaddingTop() + (contentH - ch) / 2);
                }
                left = x + lp.leftMargin;
                x = left + cw + lp.rightMargin;
            } else {
                // Frame 语义：按 LayoutParams.gravity 在内容区定位。
                Integer gv = childGravity.get(child);
                int g = gv != null ? gv : 0;
                left = group.getPaddingLeft() + lp.leftMargin;
                top = group.getPaddingTop() + lp.topMargin;
                int hg = g & 0x7;
                if (hg == 5) {  // Gravity.RIGHT
                    left = groupWidth - group.getPaddingRight() - lp.rightMargin - cw;
                } else if (hg == 1) {  // Gravity.CENTER_HORIZONTAL
                    left = group.getPaddingLeft() + (contentW - cw) / 2;
                }
                int vg = g & 0x70;
                if (vg == 0x50) {  // Gravity.BOTTOM
                    top = groupHeight - group.getPaddingBottom() - lp.bottomMargin - ch;
                } else if (vg == 0x10) {  // Gravity.CENTER_VERTICAL
                    top = group.getPaddingTop() + (contentH - ch) / 2;
                }
            }
            child.layout(left, top, left + cw, top + ch);
        }
    }

    final class HostLinearLayout extends LinearLayout {
        int gravityProp = 0;
        int orientationProp = 1;  // LinearLayout.VERTICAL

        HostLinearLayout(Context c) {
            super(c);
            setOrientation(VERTICAL);
        }

        @Override
        protected void onMeasure(int ws, int hs) {
            int mode = orientationProp == 1 ? LAYOUT_VERTICAL : LAYOUT_HORIZONTAL;
            int[] ext = measureChildrenHost(this, ws, hs, mode);
            setMeasuredDimension(View.resolveSize(ext[0], ws), View.resolveSize(ext[1], hs));
        }

        @Override
        protected void onLayout(boolean changed, int l, int t, int r, int b) {
            int mode = orientationProp == 1 ? LAYOUT_VERTICAL : LAYOUT_HORIZONTAL;
            layoutChildrenHost(this, mode, gravityProp);
        }
    }

    final class HostFrameLayout extends ViewGroup {
        HostFrameLayout(Context c) {
            super(c);
        }

        @Override
        protected ViewGroup.LayoutParams generateDefaultLayoutParams() {
            return new ViewGroup.MarginLayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT);
        }

        @Override
        protected void onMeasure(int ws, int hs) {
            int[] ext = measureChildrenHost(this, ws, hs, LAYOUT_FRAME);
            setMeasuredDimension(View.resolveSize(ext[0], ws), View.resolveSize(ext[1], hs));
        }

        @Override
        protected void onLayout(boolean changed, int l, int t, int r, int b) {
            layoutChildrenHost(this, LAYOUT_FRAME, 0);
        }
    }

    // ---- draw 命令段扫描（计数/校验；物化不消费）----

    static int scanDrawSegment(ByteBuffer buf) {
        int count = buf.getInt();
        for (int i = 0; i < count; i++) {
            int op = buf.get() & 0xFF;
            buf.get();
            buf.getShort();
            buf.position(buf.position() + 4 * 4 + 8 * 4 + 4); // f[4] i[8] color
            if (op == 9) { // kDrawText：str16
                int bytes = buf.getShort() & 0xFFFF;
                buf.position(buf.position() + bytes);
            } else if (op == 10) { // kDrawBitmap：u32 len + pixels
                int len = buf.getInt();
                buf.position(buf.position() + len * 4);
            } else {
                buf.getInt(); // u32 0
            }
        }
        return count;
    }
}
