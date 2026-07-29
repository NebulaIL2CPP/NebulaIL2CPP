package dev.nebula.il2cpp;

import android.app.Activity;
import android.graphics.PixelFormat;
import android.opengl.GLES30;
import android.opengl.GLSurfaceView;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.MotionEvent;
import android.view.KeyEvent;
import android.view.SurfaceHolder;
import android.view.ViewGroup;

import java.util.concurrent.atomic.AtomicBoolean;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * Transparent, independent GLES3 surface placed above Unity.
 *
 * It works whether Unity itself renders with Vulkan or OpenGL because this
 * view owns a separate EGL context and swap chain.
 */
public final class NebulaOverlayView extends GLSurfaceView
        implements GLSurfaceView.Renderer {
    private static final String TAG = "NebulaIL2CPP";
    private static final AtomicBoolean ATTACHED = new AtomicBoolean();
    private static volatile NebulaOverlayView instance;
    private volatile int surfaceWidth;
    private volatile int surfaceHeight;
    private boolean gestureCaptured;

    private NebulaOverlayView(Activity activity) {
        super(activity);
        setEGLContextClientVersion(3);
        setEGLConfigChooser(8, 8, 8, 8, 16, 0);
        getHolder().setFormat(PixelFormat.TRANSLUCENT);
        setZOrderOnTop(true);
        setPreserveEGLContextOnPause(true);
        setClickable(true);
        setFocusable(true);
        setFocusableInTouchMode(true);
        setRenderer(this);
        setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);
    }

    public static void attach(Activity activity) {
        if (activity == null || !ATTACHED.compareAndSet(false, true)) {
            return;
        }
        nativeUseCompatibilityRenderer();
        Handler handler = new Handler(Looper.getMainLooper());
        long[] delays = {0L, 250L, 750L, 1500L, 3000L, 5000L};
        for (long delay : delays) {
            handler.postDelayed(() -> ensureAttached(activity), delay);
        }
    }

    private static void ensureAttached(Activity activity) {
        if (activity.isFinishing() || activity.isDestroyed()) {
            return;
        }
        ViewGroup content = activity.findViewById(android.R.id.content);
        if (content == null) {
            Log.w(TAG, "Compatibility overlay: android.R.id.content not ready");
            return;
        }
        NebulaOverlayView view = instance;
        if (view == null) {
            view = new NebulaOverlayView(activity);
            instance = view;
        }
        if (view.getParent() != content) {
            if (view.getParent() instanceof ViewGroup) {
                ((ViewGroup) view.getParent()).removeView(view);
            }
            android.widget.FrameLayout.LayoutParams params =
                    new android.widget.FrameLayout.LayoutParams(
                            ViewGroup.LayoutParams.MATCH_PARENT,
                            ViewGroup.LayoutParams.MATCH_PARENT);
            content.addView(view, params);
            Log.i(TAG, "Compatibility GLSurfaceView attached");
        }
        view.setVisibility(VISIBLE);
        view.bringToFront();
        view.requestFocus();
        view.onResume();
    }

    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        Log.i(TAG, "Compatibility GLSurfaceView surface created");
        GLES30.glDisable(GLES30.GL_DEPTH_TEST);
        GLES30.glDisable(GLES30.GL_CULL_FACE);
        nativeSurfaceCreated();
    }

    @Override
    public void onSurfaceChanged(GL10 gl, int width, int height) {
        Log.i(TAG, "Compatibility GLSurfaceView surface changed: "
                + width + "x" + height);
        surfaceWidth = width;
        surfaceHeight = height;
        GLES30.glViewport(0, 0, width, height);
    }

    @Override
    public void onDrawFrame(GL10 gl) {
        nativeRender(surfaceWidth, surfaceHeight);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN) {
            // Native uses ImGui's current window bounds, so a dragged menu
            // remains interactive anywhere on the full-screen surface.
            gestureCaptured = nativeTouch(
                    action, event.getX(), event.getY());
            return gestureCaptured;
        }
        if (gestureCaptured) {
            nativeTouch(action, event.getX(), event.getY());
            if (action == MotionEvent.ACTION_UP
                    || action == MotionEvent.ACTION_CANCEL) {
                gestureCaptured = false;
            }
            return true;
        }
        return false;
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_VOLUME_UP) {
            nativeSetVisible(true);
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_VOLUME_DOWN) {
            nativeSetVisible(false);
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_VOLUME_UP
                || keyCode == KeyEvent.KEYCODE_VOLUME_DOWN) {
            return true;
        }
        return super.onKeyUp(keyCode, event);
    }

    private static native void nativeUseCompatibilityRenderer();
    private static native void nativeSurfaceCreated();
    private static native void nativeRender(int width, int height);
    private static native boolean nativeTouch(int action, float x, float y);
    private static native void nativeSetVisible(boolean visible);
}
