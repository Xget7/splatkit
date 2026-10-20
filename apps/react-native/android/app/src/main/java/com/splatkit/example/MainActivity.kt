package com.splatkit.example

import android.os.Bundle
import com.facebook.react.ReactActivity
import com.facebook.react.ReactActivityDelegate
import com.facebook.react.defaults.DefaultReactActivityDelegate

class MainActivity : ReactActivity() {

  override fun getMainComponentName(): String = "SplatKitExample"

  // SplatKitView loads absolute file paths, which src/App.tsx builds under this directory.
  // App-specific external storage needs no permission, and `adb push` fills it.
  override fun createReactActivityDelegate(): ReactActivityDelegate =
      object : DefaultReactActivityDelegate(this, mainComponentName) {
        override fun getLaunchOptions(): Bundle =
            Bundle().apply { putString("filesPath", getExternalFilesDir(null)?.path) }
      }
}
