package dev.nebula.il2cpp;

import android.app.Activity;
import android.content.ContentValues;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.ParcelFileDescriptor;
import android.provider.MediaStore;
import android.util.Log;

import java.io.File;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * Optional Java entry point for hosts that can load Nebula through JNI.
 * Native injection does not require this class: the ELF constructor starts the
 * framework as soon as libNebula.so is mapped into the target process.
 */
public final class NebulaLoader {
    private static volatile boolean loaded;
    private static volatile boolean loggingInitialized;

    private NebulaLoader() {
    }

    public static synchronized void load() {
        if (!loaded) {
            System.loadLibrary("Nebula");
            loaded = true;
        }
    }

    /**
     * Loads Nebula and installs the renderer-independent overlay.
     * Call this once from UnityPlayerActivity.onCreate().
     */
    public static void attach(Activity activity) {
        load();
        initializeFileLogging(activity);
        NebulaOverlayView.attach(activity);
    }

    private static synchronized void initializeFileLogging(
            Activity activity) {
        if (loggingInitialized) {
            return;
        }
        String timestamp = new SimpleDateFormat(
                "yyyyMMdd-HHmmss", Locale.US).format(new Date());
        String fileName = "NebulaIL2CPP-" + timestamp + ".log";
        try {
            ParcelFileDescriptor descriptor;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                ContentValues values = new ContentValues();
                values.put(MediaStore.MediaColumns.DISPLAY_NAME, fileName);
                values.put(MediaStore.MediaColumns.MIME_TYPE, "text/plain");
                values.put(
                        MediaStore.MediaColumns.RELATIVE_PATH,
                        Environment.DIRECTORY_DOWNLOADS);
                Uri uri = activity.getContentResolver().insert(
                        MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
                if (uri == null) {
                    throw new IllegalStateException(
                            "MediaStore insert returned null");
                }
                descriptor = activity.getContentResolver()
                        .openFileDescriptor(uri, "wa");
            } else {
                File file = new File(
                        Environment.getExternalStoragePublicDirectory(
                                Environment.DIRECTORY_DOWNLOADS),
                        fileName);
                descriptor = ParcelFileDescriptor.open(
                        file,
                        ParcelFileDescriptor.MODE_CREATE
                                | ParcelFileDescriptor.MODE_WRITE_ONLY
                                | ParcelFileDescriptor.MODE_APPEND);
            }
            if (descriptor == null) {
                throw new IllegalStateException(
                        "Could not open Download log file");
            }
            int fileDescriptor = descriptor.detachFd();
            if (!nativeSetLogFileDescriptor(fileDescriptor)) {
                throw new IllegalStateException(
                        "Native logger rejected file descriptor");
            }
            loggingInitialized = true;
            Log.i("NebulaIL2CPP", "Logging to Download/" + fileName);
        } catch (Throwable error) {
            Log.e(
                    "NebulaIL2CPP",
                    "Could not initialize Download file logging",
                    error);
        }
    }

    private static native boolean nativeSetLogFileDescriptor(
            int fileDescriptor);
}
