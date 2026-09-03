package com.splatkit.devapp

import android.app.Activity
import android.os.Bundle
import com.splatkit.SplatSurfaceView

class MainActivity : Activity() {
    private lateinit var splatView: SplatSurfaceView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        splatView = SplatSurfaceView(this)
        setContentView(splatView)
        // Asset reads are file IO; keep them off the UI thread.
        Thread {
            splatView.loadWorld(assets.open("kitchen_500k.spz").use { it.readBytes() })
            splatView.loadCollider(assets.open("kitchen_collider.glb").use { it.readBytes() })
        }.start()
        splatView.setMotionEnabled(true)
    }

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
