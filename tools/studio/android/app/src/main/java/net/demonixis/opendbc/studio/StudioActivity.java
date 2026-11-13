package net.demonixis.opendbc.studio;

import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Bundle;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import org.libsdl.app.SDLActivity;

// The Studio's activity: SDL's, plus what the native side cannot learn or draw by itself.
public final class StudioActivity extends SDLActivity {
    // Written on the UI thread, read by the Studio's thread.
    private static volatile int keyboardHeight;
    // Left, top, right, bottom; replaced as a whole.
    private static volatile int[] coveredEdges = new int[4];
    private static volatile boolean stopRequested;
    private View stopButton;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        super.onCreate(savedInstanceState);
        final View content = findViewById(android.R.id.content);
        content.setOnApplyWindowInsetsListener((view, insets) -> {
            keyboardHeight = coveredByKeyboard(view, insets);
            coveredEdges = coveredBySystem(insets);
            return view.onApplyWindowInsets(insets);
        });
    }

    @Override
    protected String[] getLibraries() {
        return new String[]{"main"};
    }

    // How much of the bottom of the content the on-screen keyboard covers, in pixels.
    private static int coveredByKeyboard(View content, WindowInsets insets) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            return 0;
        }
        final int keyboard = insets.getInsets(WindowInsets.Type.ime()).bottom;
        if (keyboard <= 0) {
            return 0;
        }
        final View root = content.getRootView();
        final int[] origin = new int[2];
        content.getLocationInWindow(origin);
        final int contentBottom = origin[1] + content.getHeight();
        return Math.max(0, contentBottom - (root.getHeight() - keyboard));
    }

    // What notches and the system bars now showing cover of each edge, in pixels. The bands of the
    // system's edge gestures cover nothing.
    private static int[] coveredBySystem(WindowInsets insets) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            return new int[4];
        }
        final Insets covered =
            insets.getInsets(WindowInsets.Type.displayCutout() | WindowInsets.Type.systemBars());
        return new int[]{covered.left, covered.top, covered.right, covered.bottom};
    }

    public static int[] coveredEdges() {
        return coveredEdges;
    }

    public static int keyboardHeight() {
        return keyboardHeight;
    }

    // A round button in the top right corner, over the running program, that asks for it to stop.
    public void showStopButton() {
        runOnUiThread(() -> {
            if (stopButton != null) {
                return;
            }
            final int size = dp(44);
            final ImageButton button = new ImageButton(this);
            button.setImageResource(android.R.drawable.ic_menu_close_clear_cancel);
            final GradientDrawable disc = new GradientDrawable();
            disc.setShape(GradientDrawable.OVAL);
            disc.setColor(Color.argb(150, 0, 0, 0));
            button.setBackground(disc);
            button.setOnClickListener(view -> stopRequested = true);
            final FrameLayout.LayoutParams where =
                new FrameLayout.LayoutParams(size, size, Gravity.TOP | Gravity.END);
            where.setMargins(dp(12), dp(12), dp(12), dp(12));
            ((ViewGroup) findViewById(android.R.id.content)).addView(button, where);
            stopButton = button;
        });
    }

    public void hideStopButton() {
        runOnUiThread(() -> {
            if (stopButton != null) {
                ((ViewGroup) stopButton.getParent()).removeView(stopButton);
                stopButton = null;
            }
        });
    }

    // Whether the button was pressed since the last call.
    public static boolean takeStopRequest() {
        final boolean requested = stopRequested;
        stopRequested = false;
        return requested;
    }

    private int dp(int value) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value,
            getResources().getDisplayMetrics()));
    }
}
