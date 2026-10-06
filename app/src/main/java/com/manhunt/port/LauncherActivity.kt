package com.manhunt.port

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast

class LauncherActivity : Activity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(50, 50, 50, 50)
        }

        val title = TextView(this).apply {
            text = "Manhunt Android Port"
            textSize = 24f
            setPadding(0, 0, 0, 50)
        }

        val pathInput = EditText(this).apply {
            hint = "Game Data Path (e.g. /storage/emulated/0/Manhunt)"
            val prefs = getSharedPreferences("ManhuntPrefs", MODE_PRIVATE)
            setText(prefs.getString("game_path", "/storage/emulated/0/Manhunt"))
        }

        val btnLaunch = Button(this).apply {
            text = "Launch Game"
            setOnClickListener {
                if (checkStoragePermission()) {
                    val path = pathInput.text.toString()
                    getSharedPreferences("ManhuntPrefs", MODE_PRIVATE).edit().putString("game_path", path).apply()
                    
                    val intent = Intent(this@LauncherActivity, MainActivity::class.java)
                    intent.putExtra("GAME_PATH", path)
                    startActivity(intent)
                }
            }
        }

        layout.addView(title)
        layout.addView(pathInput)
        layout.addView(btnLaunch)

        setContentView(layout)
    }

    private fun checkStoragePermission(): Boolean {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                try {
                    val intent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION)
                    intent.addCategory("android.intent.category.DEFAULT")
                    intent.data = Uri.parse("package:${applicationContext.packageName}")
                    startActivity(intent)
                } catch (e: Exception) {
                    val intent = Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)
                    startActivity(intent)
                }
                Toast.makeText(this, "Please grant All Files Access", Toast.LENGTH_LONG).show()
                return false
            }
        } else {
            if (checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                requestPermissions(arrayOf(
                    android.Manifest.permission.READ_EXTERNAL_STORAGE,
                    android.Manifest.permission.WRITE_EXTERNAL_STORAGE
                ), 1)
                return false
            }
        }
        return true
    }
}
