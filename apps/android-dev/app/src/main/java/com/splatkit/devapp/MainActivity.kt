package com.splatkit.devapp

import android.app.Activity
import android.content.Intent
import android.graphics.Insets
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.view.Gravity
import android.view.View
import android.view.WindowInsets
import android.view.WindowManager
import android.widget.Button
import android.widget.FrameLayout
import android.widget.Toast
import com.splatkit.CameraPose
import com.splatkit.CoordinateFrame
import com.splatkit.RenderPolicy
import com.splatkit.RenderQuality
import com.splatkit.SplatSurfaceView
import com.splatkit.ui.JoystickView
import com.splatkit.ui.SplatHudView
import java.io.File

private const val TAG = "SplatKitDev"
/** scripts/android_perf.py fails fast on this logcat marker. */
private const val RUN_INVALID = "SplatRunInvalid:"
private const val RUN_ACCEPTED = "SplatRun:"
/** scripts/android_perf.py waits for this marker in logcat. */
private const val WORLD_FAILED = "World failed:"
private const val BUNDLED_WORLD = "kitchen_500k.spz"
private const val BUNDLED_COLLIDER = "kitchen_collider.glb"
private const val TILED_WORLD_SUFFIX = ".json"

/**
 * Development screen: the splat view under a HUD, a walk joystick bottom left and a
 * gyroscope toggle bottom right. Dragging elsewhere turns the camera.
 */
class MainActivity : Activity() {
    private lateinit var splatView: SplatSurfaceView

    /** The run's pose, applied once the world (and its collider, if any) is up. */
    private var pendingPose: CameraPose? = null
    /** The run applied last, so a second intent naming the same world changes settings without reloading. */
    private var appliedRun: RunFile? = null
    /** The HUD, joystick and gyroscope toggle, hidden during a scripted run. */
    private val overlays = mutableListOf<View>()
    private lateinit var gyroButton: Button

    private fun applyPendingPose() {
        pendingPose?.let {
            splatView.cameraPose = it
            Log.i(TAG, "camera pose set to $it")
        }
    }

    private fun refreshGyroLabel() {
        gyroButton.text = if (splatView.isMotionEnabled) "Gyro: on" else "Gyro: off"
    }

    /** Meters per second at full joystick deflection. */
    private val walkSpeed = 1.5f

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Log.i(TAG, "lifecycle: create")
        val density = resources.displayMetrics.density
        fun dp(value: Int) = (value * density).toInt()

        splatView = SplatSurfaceView(this)
        val hud = SplatHudView(this).apply { attach(splatView) }
        val joystick = JoystickView(this).apply {
            onMove = { x, y -> splatView.setWalkVelocity(y * walkSpeed, x * walkSpeed) }
        }
        gyroButton = Button(this)
        gyroButton.setOnClickListener {
            splatView.setMotionEnabled(!splatView.isMotionEnabled)
            refreshGyroLabel()
        }

        // The splats fill the notch's edge too; the controls stay clear of it.
        window.attributes = window.attributes.apply {
            layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false)
        } else {
            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_LAYOUT_STABLE or
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
        }
        val root = FrameLayout(this)
        root.addView(splatView, FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT)
        val controls = FrameLayout(this)
        controls.setOnApplyWindowInsetsListener { view, insets ->
            val safe = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout())
            } else {
                @Suppress("DEPRECATION")
                Insets.of(insets.systemWindowInsetLeft, insets.systemWindowInsetTop,
                    insets.systemWindowInsetRight, insets.systemWindowInsetBottom)
            }
            view.setPadding(safe.left, safe.top, safe.right, safe.bottom)
            insets
        }
        root.addView(controls, FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT)
        controls.addView(hud, FrameLayout.LayoutParams(FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT).apply {
            gravity = Gravity.TOP or Gravity.START
            setMargins(dp(12), dp(40), 0, 0)
        })
        controls.addView(joystick, FrameLayout.LayoutParams(dp(140), dp(140)).apply {
            gravity = Gravity.BOTTOM or Gravity.START
            setMargins(dp(24), 0, 0, dp(40))
        })
        controls.addView(gyroButton, FrameLayout.LayoutParams(FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT).apply {
            gravity = Gravity.BOTTOM or Gravity.END
            setMargins(0, 0, dp(24), dp(40))
        })
        setContentView(root)
        overlays += listOf(hud, joystick, gyroButton)

        // A world pushed to the app's external files dir loads instead of the bundled kitchen when a
        // run file (scripts/benchmarks/run.schema.json) names it. The file goes whole in the run
        // extra; the inner quotes survive the device shell's second parse of the adb arguments:
        //   adb push house.spz /sdcard/Android/data/com.splatkit.devapp/files/
        //   adb shell am start -n com.splatkit.devapp/.MainActivity --es run "'$(cat run.json)'"
        splatView.listener = object : SplatSurfaceView.Listener {
            override fun onWorldReady(splatCount: Int) {
                Log.i(TAG, "world ready: $splatCount splats")
                applyPendingPose()
            }
            override fun onWorldFrameReady(splatCount: Int) {
                Log.i(TAG, "world frame ready: $splatCount splats")
            }
            override fun onWorldFailed(message: String) = toast("$WORLD_FAILED $message")
            override fun onColliderReady() {
                Log.i(TAG, "collider ready, walking")
                applyPendingPose()
            }
            override fun onColliderFailed(message: String) = toast("Collider failed: $message")
        }
        if (!splatView.isAvailable) toast("Vulkan is not available on this device")
        applyIntent(intent)
    }

    private fun toast(message: String) {
        Log.e(TAG, message)
        Toast.makeText(this, message, Toast.LENGTH_LONG).show()
    }

    /**
     * Applies the run file in the run extra; without extras, the bundled world for interactive use.
     * Also runs for an intent delivered to the running activity (singleTop), so a
     * second `am start` switches worlds in place, as a host would.
     */
    private fun applyIntent(intent: Intent?) {
        val extras = intent?.extras
        val run = when (val launch = Launch.fromExtras(extras?.keySet().orEmpty(), extras?.getString(RUN_EXTRA))) {
            Launch.Default -> RunFile()
            is Launch.Run -> launch.file.also { Log.i(TAG, "$RUN_ACCEPTED ${launch.json}") }
            is Launch.Invalid -> return toast("$RUN_INVALID ${launch.message}")
        }
        applyRenderer(run.renderer)
        val scripted = run.run
        overlays.forEach { it.visibility = if (scripted == null) View.VISIBLE else View.GONE }
        if (scripted == null) {
            window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        } else {
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
        splatView.touchLookEnabled = scripted == null
        // Off in every mode, so a pose is held exactly; the button turns it on.
        splatView.setMotionEnabled(false)
        refreshGyroLabel()

        // The same world in a second intent keeps what is loaded: the settings above switch in place.
        val previous = appliedRun
        val reload = previous == null || previous.world != run.world
        Log.i(TAG, "world request: ${run.world} reload=$reload")
        appliedRun = run
        pendingPose = run.pose
        if (reload) loadWorld(run.world) else applyPendingPose()
        scripted?.let { splatView.startBenchmark(it.seconds) }
    }

    /** The quality level first, then each field the run gives. */
    private fun applyRenderer(renderer: RunRenderer) {
        splatView.applyQuality(renderer.quality ?: RenderQuality.HIGH)
        renderer.budget?.let { splatView.splatBudget = it }
        if (renderer.subpixel == null && renderer.lodErrorPixels == null && renderer.lodSplatLimit == null) return
        val current = splatView.renderPolicy ?: RenderPolicy()
        splatView.applyRenderPolicy(
            current.copy(
                lodSplatLimit = renderer.lodSplatLimit ?: current.lodSplatLimit,
                lodErrorPixels = renderer.lodErrorPixels ?: current.lodErrorPixels,
                subpixelThreshold = renderer.subpixel ?: current.subpixelThreshold,
            ),
        ) { Log.i(TAG, "render policy: ${it.effective} ${it.warnings}") }
    }

    /** Null loads the bundled kitchen, an untagged World Labs world, in rdf. */
    private fun loadWorld(world: RunWorld?) {
        splatView.sourceFrame = if (world == null) CoordinateFrame.RDF else world.sourceFrame
        // File reads are IO; keep them off the UI thread.
        Thread {
            try {
                if (world == null) {
                    splatView.loadWorld(assets.open(BUNDLED_WORLD).use { it.readBytes() })
                    splatView.loadCollider(assets.open(BUNDLED_COLLIDER).use { it.readBytes() })
                } else {
                    val file = externalFile(world.file)
                    if (world.file.endsWith(TILED_WORLD_SUFFIX, ignoreCase = true)) splatView.loadTiledWorld(file) else splatView.loadWorld(file)
                    world.collider?.let { splatView.loadCollider(externalFile(it)) }
                }
            } catch (e: java.io.IOException) {
                Log.e(TAG, "$WORLD_FAILED ${e.message}")
                runOnUiThread { toast("Could not read the world: ${e.message}") }
            }
        }.start()
    }

    override fun onNewIntent(intent: Intent?) {
        super.onNewIntent(intent)
        setIntent(intent)
        applyIntent(intent)
    }

    /** Absolute paths are used as given; anything else is relative to the app's external files dir. */
    private fun externalFile(path: String): File =
        if (path.startsWith("/")) File(path) else File(getExternalFilesDir(null), path)

    override fun onResume() {
        super.onResume()
        Log.i(TAG, "lifecycle: resume")
        splatView.resume()
    }

    override fun onPause() {
        Log.i(TAG, "lifecycle: pause")
        splatView.pause()
        super.onPause()
    }

    override fun onDestroy() {
        Log.i(TAG, "lifecycle: destroy")
        splatView.release()
        super.onDestroy()
    }
}
