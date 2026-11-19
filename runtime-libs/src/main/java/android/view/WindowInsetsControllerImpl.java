package android.view;

/** WindowInsetsController 的 VM no-op 实现（系统栏状态本地记录）。 */
class WindowInsetsControllerImpl implements WindowInsetsController {
    private int mBehavior;
    private int mAppearance;

    @Override
    public void show(int types) {
    }

    @Override
    public void hide(int types) {
    }

    @Override
    public int getSystemBarsBehavior() {
        return mBehavior;
    }

    @Override
    public void setSystemBarsBehavior(int behavior) {
        mBehavior = behavior;
    }

    @Override
    public void setSystemBarsAppearance(int appearance, int mask) {
        mAppearance = (mAppearance & ~mask) | (appearance & mask);
    }

    @Override
    public int getSystemBarsAppearance() {
        return mAppearance;
    }

    @Override
    public void addOnControllableInsetsChangedListener(
            OnControllableInsetsChangedListener listener) {
    }

    @Override
    public void removeOnControllableInsetsChangedListener(
            OnControllableInsetsChangedListener listener) {
    }
}
