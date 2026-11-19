package android.media;

/**
 * 媒体播放器（AOSP android.media.MediaPlayer 公开面；§31.7 C 档 lib——VM 无
 * 音频/视频播放桥，状态机按调用接受、时长/播放位为本地字段；像素/声音归宿主
 * 桥接后置）。真实音频播放经 VideoView 桥接面由宿主承担。
 */
public class MediaPlayer {
    public static final int MEDIA_INFO_UNKNOWN = 1;
    public static final int MEDIA_ERROR_UNKNOWN = 1;
    private int mDuration;
    private boolean mPlaying;
    private boolean mPrepared;
    private boolean mLooping;
    private float mVolumeLeft = 1.0f;
    private float mVolumeRight = 1.0f;

    public interface OnPreparedListener {
        void onPrepared(MediaPlayer mp);
    }

    public interface OnCompletionListener {
        void onCompletion(MediaPlayer mp);
    }

    public interface OnErrorListener {
        boolean onError(MediaPlayer mp, int what, int extra);
    }

    public MediaPlayer() {
    }

    public static MediaPlayer create(android.content.Context context, int resId) {
        return new MediaPlayer();
    }

    public void setDataSource(String path) {
    }

    public void setDataSource(android.content.Context context, android.net.Uri uri) {
    }

    public void setDataSource(java.io.FileDescriptor fd) {
    }

    public void prepare() {
        mPrepared = true;
    }

    public void prepareAsync() {
        mPrepared = true;
    }

    public void start() {
        mPlaying = true;
    }

    public void stop() {
        mPlaying = false;
    }

    public void pause() {
        mPlaying = false;
    }

    public void reset() {
        mPlaying = false;
        mPrepared = false;
        mDuration = 0;
    }

    public void release() {
        mPlaying = false;
    }

    public void seekTo(int msec) {
    }

    public void setLooping(boolean looping) {
        mLooping = looping;
    }

    public boolean isLooping() {
        return mLooping;
    }

    public void setVolume(float leftVolume, float rightVolume) {
        mVolumeLeft = leftVolume;
        mVolumeRight = rightVolume;
    }

    public void setAudioStreamType(int streamType) {
    }

    public void setAudioAttributes(Object attributes) {
    }

    public boolean setPreferredDevice(AudioDeviceInfo deviceInfo) {
        return true;
    }

    public int getDuration() {
        return mDuration > 0 ? mDuration : 1000;
    }

    public int getCurrentPosition() {
        return 0;
    }

    public boolean isPlaying() {
        return mPlaying;
    }

    public void setOnPreparedListener(OnPreparedListener listener) {
    }

    public void setOnCompletionListener(OnCompletionListener listener) {
    }

    public void setOnErrorListener(OnErrorListener listener) {
    }

    public void setOnBufferingUpdateListener(Object listener) {
    }

    public void setOnInfoListener(Object listener) {
    }

    public void setWakeMode(android.content.Context context, int mode) {
    }
}
