package com.splatkit.example

import android.os.Bundle
import com.facebook.react.ReactActivity
import com.facebook.react.ReactActivityDelegate
import com.facebook.react.defaults.DefaultNewArchitectureEntryPoint.fabricEnabled
import com.facebook.react.defaults.DefaultReactActivityDelegate
import java.io.File

class MainActivity : ReactActivity() {

  override fun getMainComponentName(): String = "SplatKitExample"

  // SplatKitView loads absolute file paths. App-specific external storage needs no permission,
  // and `adb push` fills it.
  override fun createReactActivityDelegate(): ReactActivityDelegate =
      object : DefaultReactActivityDelegate(this, mainComponentName, fabricEnabled) {
        override fun getLaunchOptions(): Bundle =
            Bundle().apply { putString("worldPath", File(getExternalFilesDir(null), "world.spz").path) }
      }
}
