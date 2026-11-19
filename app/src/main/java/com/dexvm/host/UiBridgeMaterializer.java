package com.dexvm.host;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.Drawable;
import android.util.Log;
import android.util.TypedValue;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.RelativeLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.VideoView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
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
    private View root;
    private int appliedRevision = 0;
    private int dirtyCount = 0;
    private String apkPath;
    private final Map<Integer, String> appliedVideoPath = new HashMap<>();

    public UiBridgeMaterializer(Context context) {
        this.context = context;
    }

    /** 目标 APK 路径（VideoView 的 android.resource:// 源从 APK 抽包真播放）。 */
    public void setApkPath(String path) {
        this.apkPath = path;
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
        appliedVideoPath.clear();
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
        root = materializeTree(roots.get(0));
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

    private View materializeLeaf(Node node) {
        // 按类名后缀匹配宿主控件：知乎大量使用自定义子类（ZHImageView、
        // ZTextView、AppCompatTextView…），精确匹配会全部退化成通用容器，
        // 图片/文字等属性无法物化（开屏/骨架因此整片空白）。
        String cls = node.className != null ? node.className : "";
        View v;
        if (cls.endsWith("EditText")) {
            v = new EditText(context);
        } else if (cls.endsWith("ImageButton")) {
            // ImageButton 以图片出画（AOSP ImageButton extends ImageView）。
            v = new ImageView(context);
        } else if (cls.endsWith("Button") || cls.contains("Button")) {
            v = new Button(context);
        } else if (cls.contains("Switch")) {
            v = new Switch(context);
        } else if (cls.endsWith("TextView")) {
            v = new TextView(context);
        } else if (cls.endsWith("ImageView")) {
            v = new ImageView(context);
        } else if (cls.contains("ProgressBar")) {
            v = new ProgressBar(context, null, android.R.attr.progressBarStyleHorizontal);
        } else if (cls.endsWith("VideoView")) {
            v = new VideoView(context);
        } else if (cls.endsWith("LinearLayout")) {
            v = new HostLinearLayout(context);
        } else {
            // FrameLayout/RelativeLayout/ScrollView/ViewGroup/自定义容器：
            // 通用容器承载，子节点可挂。
            v = new HostViewGroup(context);
        }
        applyProps(v, node);
        wireClick(v, node.guestViewId);
        return v;
    }

    private void attach(View parent, View child, Node node, Node parentNode) {
        if (!(parent instanceof ViewGroup)) {
            Log.w(TAG, "attach skipped: parent not a group (" + node.className + ")");
            return;
        }
        ViewGroup group = (ViewGroup) parent;
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
        // guest bounds 是窗口绝对坐标；宿主物化容器按父子相对位摆放（父节点
        // 自身已带绝对偏移），child_margin = child_abs - parent_abs。
        Prop pbounds = parentNode != null ? parentNode.prop(PROP_BOUNDS) : null;
        int originX = pbounds != null ? pbounds.rect[0] : 0;
        int originY = pbounds != null ? pbounds.rect[1] : 0;
        ViewGroup.MarginLayoutParams lp = new ViewGroup.MarginLayoutParams(
                Math.max(w, 0), Math.max(h, 0));
        lp.leftMargin = left - originX;
        lp.topMargin = top - originY;
        group.addView(child, lp);
    }

    private void applyProps(View v, Node node) {
        if (v instanceof TextView) {
            TextView tv = (TextView) v;
            Prop text = node.prop(PROP_TEXT);
            if (text != null && text.s != null) tv.setText(text.s);
            Prop ts = node.prop(PROP_TEXT_SIZE);
            if (ts != null) {
                tv.setTextSize(TypedValue.COMPLEX_UNIT_PX, Math.max(ts.f, 1f));
            }
            Prop tc = node.prop(PROP_TEXT_COLOR);
            if (tc != null) tv.setTextColor(tc.asInt());
            Prop gv = node.prop(PROP_GRAVITY);
            if (gv != null && gv.asInt() != 0) tv.setGravity(gv.asInt());
        }
        if (v instanceof ImageView) {
            Prop img = node.prop(PROP_IMAGE_BYTES);
            if (img != null && img.bytes != null && img.bytes.length > 0) {
                Bitmap bmp = BitmapFactory.decodeByteArray(img.bytes, 0, img.bytes.length);
                if (bmp != null) {
                    ((ImageView) v).setImageDrawable(new BitmapDrawable(context.getResources(), bmp));
                    ((ImageView) v).setScaleType(ImageView.ScaleType.FIT_CENTER);
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
        if (v instanceof VideoView) {
            applyVideoProps((VideoView) v, node);
        }
        if (v instanceof HostLinearLayout) {
            Prop g = node.prop(PROP_GRAVITY);
            if (g != null) ((HostLinearLayout) v).gravityProp = g.asInt();
        }
        Prop pad = node.prop(PROP_PADDING);
        if (pad != null) {
            v.setPadding(pad.rect[0], pad.rect[1], pad.rect[2], pad.rect[3]);
        }
        Prop vis = node.prop(PROP_VISIBILITY);
        if (vis != null) v.setVisibility(vis.asInt());
        Prop alpha = node.prop(PROP_ALPHA);
        if (alpha != null) v.setAlpha(Math.max(0f, Math.min(alpha.f, 1f)));
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

    private void wireClick(final View v, final int guestViewId) {
        v.setClickable(true);
        v.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View vv) {
                String status;
                try {
                    status = NativeBridge.nativeDispatchViewClick(guestViewId);
                } catch (Throwable t) {
                    status = "error: " + t;
                }
                Log.i(TAG, "host click viewId=" + guestViewId + " -> " + status);
            }
        });
    }

    // ---- 增量 patch ----

    private void applyOps(List<PatchOp> ops) {
        for (PatchOp op : ops) {
            switch (op.op) {
                case PATCH_ADD: {
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
                    if (parent != null) attach(parent, v, node, parentNode);
                    break;
                }
                case PATCH_REMOVE: {
                    View v = views.remove(op.objectId);
                    nodes.remove(op.objectId);
                    if (v != null && v.getParent() instanceof ViewGroup) {
                        ((ViewGroup) v.getParent()).removeView(v);
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
                    wireClick(v, node.guestViewId);
                    // bounds 变更 -> 重新布局位（父容器 lp 更新；绝对->父相对）
                    Prop bounds = node.prop(PROP_BOUNDS);
                    if (bounds != null && v.getLayoutParams() instanceof ViewGroup.MarginLayoutParams) {
                        ViewGroup.MarginLayoutParams lp =
                                (ViewGroup.MarginLayoutParams) v.getLayoutParams();
                        Node parentNode = nodes.get(node.parentId);
                        Prop pbounds = parentNode != null ? parentNode.prop(PROP_BOUNDS) : null;
                        int originX = pbounds != null ? pbounds.rect[0] : 0;
                        int originY = pbounds != null ? pbounds.rect[1] : 0;
                        lp.leftMargin = bounds.rect[0] - originX;
                        lp.topMargin = bounds.rect[1] - originY;
                        lp.width = Math.max(bounds.rect[2] - bounds.rect[0], 0);
                        lp.height = Math.max(bounds.rect[3] - bounds.rect[1], 0);
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

    // ---- 物化容器（bounds 绝对摆放；不重跑宿主 measure 语义）----

    /** 子 View 按 guest bounds 绝对 measure；返回内容包围盒 (w,h)。 */
    private static int[] measureChildrenAbsolute(ViewGroup group) {
        int w = 0;
        int h = 0;
        for (int i = 0; i < group.getChildCount(); i++) {
            View child = group.getChildAt(i);
            ViewGroup.MarginLayoutParams lp = (ViewGroup.MarginLayoutParams) child.getLayoutParams();
            int cw = Math.max(lp.width, 0);
            int ch = Math.max(lp.height, 0);
            child.measure(View.MeasureSpec.makeMeasureSpec(cw, View.MeasureSpec.EXACTLY),
                    View.MeasureSpec.makeMeasureSpec(ch, View.MeasureSpec.EXACTLY));
            w = Math.max(w, lp.leftMargin + child.getMeasuredWidth());
            h = Math.max(h, lp.topMargin + child.getMeasuredHeight());
        }
        return new int[] {w, h};
    }

    private static void layoutChildrenAbsolute(ViewGroup group) {
        for (int i = 0; i < group.getChildCount(); i++) {
            View child = group.getChildAt(i);
            ViewGroup.MarginLayoutParams lp = (ViewGroup.MarginLayoutParams) child.getLayoutParams();
            int l = lp.leftMargin;
            int t = lp.topMargin;
            child.layout(l, t, l + child.getMeasuredWidth(), t + child.getMeasuredHeight());
        }
    }

    static final class HostLinearLayout extends LinearLayout {
        int gravityProp = 0;

        HostLinearLayout(Context c) {
            super(c);
            setOrientation(VERTICAL);
        }

        @Override
        protected void onMeasure(int ws, int hs) {
            int[] ext = measureChildrenAbsolute(this);
            setMeasuredDimension(View.resolveSize(ext[0], ws), View.resolveSize(ext[1], hs));
        }

        @Override
        protected void onLayout(boolean changed, int l, int t, int r, int b) {
            int groupWidth = r - l;
            for (int i = 0; i < getChildCount(); i++) {
                View child = getChildAt(i);
                ViewGroup.MarginLayoutParams lp =
                        (ViewGroup.MarginLayoutParams) child.getLayoutParams();
                int cw = child.getMeasuredWidth();
                int ch = child.getMeasuredHeight();
                int left = lp.leftMargin;
                if ((gravityProp & 0x1) != 0) {  // Gravity.CENTER_HORIZONTAL
                    // 宿主控件自量后可能窄于布局宽（VideoView 按视频宽高比自校）：
                    // 按 AOSP LinearLayout 水平居中语义落位。
                    left = Math.max(0, (groupWidth - cw) / 2);
                }
                int top = lp.topMargin;
                child.layout(left, top, left + cw, top + ch);
            }
        }
    }

    static final class HostViewGroup extends ViewGroup {
        HostViewGroup(Context c) {
            super(c);
        }

        @Override
        protected ViewGroup.LayoutParams generateDefaultLayoutParams() {
            return new ViewGroup.MarginLayoutParams(0, 0);
        }

        @Override
        protected void onMeasure(int ws, int hs) {
            int[] ext = measureChildrenAbsolute(this);
            setMeasuredDimension(View.resolveSize(ext[0], ws), View.resolveSize(ext[1], hs));
        }

        @Override
        protected void onLayout(boolean changed, int l, int t, int r, int b) {
            layoutChildrenAbsolute(this);
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
