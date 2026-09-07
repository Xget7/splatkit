package com.splatkit.devapp

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.util.Log
import android.view.Gravity
import android.widget.Button
import android.widget.FrameLayout
import android.widget.Toast
import com.splatkit.CameraPose
import com.splatkit.RenderQuality
import com.splatkit.SplatSurfaceView
import com.splatkit.ui.JoystickView
import com.splatkit.ui.SplatHudView
import java.io.File

/**
 * Development screen: the splat view under a HUD, a walk joystick bottom left and a
 * gyroscope toggle bottom right. Dragging elsewhere turns the camera.
 */
private const val TAG = "SplatKitDev"

class MainActivity : Activity() {
    private lateinit var splatView: SplatSurfaceView

    /** Meters per second at full joystick deflection. */
    private val walkSpeed = 1.5f

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val density = resources.displayMetrics.density
        fun dp(value: Int) = (value * density).toInt()

        splatView = SplatSurfaceView(this)
        val hud = SplatHudView(this).apply { attach(splatView) }
        val joystick = JoystickView(this).apply {
            onMove = { x, y -> splatView.setWalkVelocity(y * walkSpeed, x * walkSpeed) }
        }
        val gyroButton = Button(this)
        fun refreshGyroLabel() {
            gyroButton.text = if (splatView.isMotionEnabled) "Gyro: on" else "Gyro: off"
        }
        gyroButton.setOnClickListener {
            splatView.setMotionEnabled(!splatView.isMotionEnabled)
            refreshGyroLabel()
        }

        val root = FrameLayout(this)
        root.addView(splatView, FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT)
        root.addView(hud, FrameLayout.LayoutParams(FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT).apply {
            gravity = Gravity.TOP or Gravity.START
            setMargins(dp(12), dp(40), 0, 0)
        })
        root.addView(joystick, FrameLayout.LayoutParams(dp(140), dp(140)).apply {
            gravity = Gravity.BOTTOM or Gravity.START
            setMargins(dp(24), 0, 0, dp(40))
        })
        root.addView(gyroButton, FrameLayout.LayoutParams(FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT).apply {
            gravity = Gravity.BOTTOM or Gravity.END
            setMargins(0, 0, dp(24), dp(40))
        })
        setContentView(root)

        // A world pushed to the app's external files dir loads instead of the bundled kitchen:
        //   adb push house.spz /sdcard/Android/data/com.splatkit.devapp/files/
        //   adb shell am start -n com.splatkit.devapp/.MainActivity --es world house.spz --es collider house.glb
        splatView.listener = object : SplatSurfaceView.Listener {
            override fun onWorldReady(splatCount: Int) {
                Log.i(TAG, "world ready: $splatCount splats")
            }
            override fun onWorldFailed(message: String) = toast("World failed: $message")
            override fun onColliderReady() {
                Log.i(TAG, "collider ready, walking")
            }
            override fun onColliderFailed(message: String) = toast("Collider failed: $message")
        }
        if (!splatView.isAvailable) toast("Vulkan is not available on this device")
        applyIntent(intent)
        refreshGyroLabel()
    }

    private fun toast(message: String) {
        Log.e(TAG, message)
        Toast.makeText(this, message, Toast.LENGTH_LONG).show()
    }

    /**
     * Applies the launch extras: preset, overrides, world and collider files, benchmark.
     * Also runs for an intent delivered to the running activity (singleTop), so a
     * second `am start` switches worlds in place, as a host would.
     */
    private fun applyIntent(intent: Intent?) {
        // --es quality low|medium|high|ultra applies a preset; the extras below override it.
        val quality = intent?.getStringExtra("quality")?.let { RenderQuality.named(it) } ?: RenderQuality.HIGH
        splatView.applyQuality(quality)
        // adb shell am start -n com.splatkit.devapp/.MainActivity --ez benchmark true --ef scale 0.7
        if (intent?.hasExtra("scale") == true) splatView.renderScale = intent.getFloatExtra("scale", 1f)
        // --ei sh 0 drops spherical harmonics for an A/B against the same file.
        if (intent?.hasExtra("sh") == true) splatView.maxShDegree = intent.getIntExtra("sh", 3)
        // --ei budget 500000 draws at most that many splats per frame through a level of detail tree.
        if (intent?.hasExtra("budget") == true) splatView.splatBudget = intent.getIntExtra("budget", 0)
        // --ez linear true blends in linear light, the old default, 40% slower.
        if (intent?.hasExtra("linear") == true) splatView.linearBlending = intent.getBooleanExtra("linear", false)
        // --ef margin 20 widens the angular margin the cull keeps drawn around the view.
        if (intent?.hasExtra("margin") == true) splatView.cullMarginDegrees = intent.getFloatExtra("margin", 10f)
        val worldPath = intent?.getStringExtra("world")
        val colliderPath = intent?.getStringExtra("collider")
        // File reads are IO; keep them off the UI thread.
        Thread {
            try {
                if (worldPath == null) {
                    splatView.loadWorld(assets.open("kitchen_500k.spz").use { it.readBytes() })
                    splatView.loadCollider(assets.open("kitchen_collider.glb").use { it.readBytes() })
                } else if (intent.getBooleanExtra("bytes", false)) {
                    // --ez bytes true goes through the ByteArray overloads instead of the files.
                    splatView.loadWorld(externalFile(worldPath).readBytes())
                    colliderPath?.let { splatView.loadCollider(externalFile(it).readBytes()) }
                } else {
                    splatView.loadWorld(externalFile(worldPath))
                    colliderPath?.let { splatView.loadCollider(externalFile(it)) }
                }
            } catch (e: java.io.IOException) {
                runOnUiThread { toast("Could not read the world: ${e.message}") }
            }
        }.start()
        // --es pose "x,y,z,yaw,pitch" teleports (meters, radians) once the world is up.
        intent?.getStringExtra("pose")?.split(",")?.map { it.trim().toFloat() }?.takeIf { it.size == 5 }?.let {
            splatView.cameraPose = CameraPose(it[0], it[1], it[2], it[3], it[4])
        }
        // --ef walk 1.0 walks forward at that speed in m/s, for checking the collider from adb.
        if (intent?.hasExtra("walk") == true) splatView.setWalkVelocity(intent.getFloatExtra("walk", 0f), 0f)
        if (intent?.getBooleanExtra("benchmark", false) == true) {
            // --ef seconds 3 turns the full circle in 3 s: a fast turn, for the cull margin.
            splatView.startBenchmark(intent.getFloatExtra("seconds", 10f))
        } else {
            splatView.setMotionEnabled(true)
        }
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
        splatView.resume()
    }

    override fun onPause() {
        splatView.pause()
        super.onPause()
    }

    override fun onDestroy() {
        splatView.release()
        super.onDestroy()
    }
}
