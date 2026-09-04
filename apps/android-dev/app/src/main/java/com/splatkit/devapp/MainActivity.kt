package com.splatkit.devapp

import android.app.Activity
import android.os.Bundle
import java.io.File
import android.view.Gravity
import android.widget.Button
import android.widget.FrameLayout
import com.splatkit.SplatSurfaceView
import com.splatkit.ui.JoystickView
import com.splatkit.ui.SplatHudView

/**
 * Development screen: the splat view under a HUD, a walk joystick bottom left and a
 * gyroscope toggle bottom right. Dragging elsewhere turns the camera.
 */
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
        val worldPath = intent?.getStringExtra("world")
        val colliderPath = intent?.getStringExtra("collider")
        // File reads are IO; keep them off the UI thread.
        Thread {
            if (worldPath == null) {
                splatView.loadWorld(assets.open("kitchen_500k.spz").use { it.readBytes() })
                splatView.loadCollider(assets.open("kitchen_collider.glb").use { it.readBytes() })
            } else {
                splatView.loadWorld(externalFile(worldPath).readBytes())
                colliderPath?.let { splatView.loadCollider(externalFile(it).readBytes()) }
            }
        }.start()
        // adb shell am start -n com.splatkit.devapp/.MainActivity --ez benchmark true --ef scale 0.7
        intent?.getFloatExtra("scale", 1f)?.let { splatView.renderScale = it }
        if (intent?.getBooleanExtra("benchmark", false) == true) {
            splatView.startBenchmark(10f)
        } else {
            splatView.setMotionEnabled(true)
        }
        refreshGyroLabel()
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
