package com.manhunt.port

import android.app.Activity
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.view.MotionEvent
import android.content.ClipData
import android.content.ClipboardManager
import android.widget.Button
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import kotlin.math.hypot
import kotlin.math.sqrt

class MainActivity : Activity() {

    private lateinit var glView: GLSurfaceView
    private lateinit var renderer: ManhuntRenderer

    // ── Estado del joystick izquierdo (movimiento) ────────────────────────────
    private var leftPointerId = -1
    private var leftStartX = 0f
    private var leftStartY = 0f
    private val JOYSTICK_RADIUS = 200f   // píxeles

    // ── Estado del toque derecho (mirar) ─────────────────────────────────────
    private var rightPointerId = -1
    private var rightPrevX = 0f
    private var rightPrevY = 0f

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        hideSystemUI()

        val gamePath = intent.getStringExtra("GAME_PATH") ?: ""
        renderer = ManhuntRenderer(assets, gamePath)

        glView = object : GLSurfaceView(this) {
            override fun onTouchEvent(e: MotionEvent): Boolean {
                val action = e.actionMasked
                val pIdx   = e.actionIndex
                val pId    = e.getPointerId(pIdx)
                val screenW = width

                when (action) {
                    MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                        val px = e.getX(pIdx)
                        val py = e.getY(pIdx)

                        if (px < screenW / 2f) {
                            // Lado izquierdo → joystick de movimiento
                            leftPointerId = pId
                            leftStartX    = px
                            leftStartY    = py
                            renderer.nativeMove(0f, 0f)
                        } else {
                            // Lado derecho → mirar
                            rightPointerId = pId
                            rightPrevX     = px
                            rightPrevY     = py
                        }
                    }

                    MotionEvent.ACTION_MOVE -> {
                        for (i in 0 until e.pointerCount) {
                            val id = e.getPointerId(i)
                            val cx = e.getX(i)
                            val cy = e.getY(i)

                            when (id) {
                                leftPointerId -> {
                                    // Joystick: offset desde el centro inicial
                                    var dx = cx - leftStartX
                                    var dy = cy - leftStartY
                                    val dist = hypot(dx, dy)
                                    if (dist > JOYSTICK_RADIUS) {
                                        dx *= JOYSTICK_RADIUS / dist
                                        dy *= JOYSTICK_RADIUS / dist
                                    }
                                    val fwd   = -(dy / JOYSTICK_RADIUS)   // arriba = adelante
                                    val right =  (dx / JOYSTICK_RADIUS)
                                    renderer.nativeMove(fwd, right)
                                }
                                rightPointerId -> {
                                    renderer.nativeLook(cx - rightPrevX, cy - rightPrevY)
                                    rightPrevX = cx
                                    rightPrevY = cy
                                }
                            }
                        }
                    }

                    MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                        when (pId) {
                            leftPointerId -> {
                                leftPointerId = -1
                                renderer.nativeMove(0f, 0f)
                            }
                            rightPointerId -> {
                                rightPointerId = -1
                                renderer.nativeLook(0f, 0f)
                            }
                        }
                    }

                    MotionEvent.ACTION_CANCEL -> {
                        leftPointerId  = -1
                        rightPointerId = -1
                        renderer.nativeMove(0f, 0f)
                    }
                }
                return true
            }
        }.apply {
            setEGLContextClientVersion(3)
            setRenderer(renderer)
            renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
        }

        setContentView(R.layout.activity_main)
        val container = findViewById<android.widget.FrameLayout>(R.id.main_container)
        container.addView(glView, 0)
        
        findViewById<android.widget.Button>(R.id.btn_jump).setOnClickListener {
            renderer.nativeJump()
        }
        
        findViewById<android.widget.Button>(R.id.btn_action).setOnClickListener {
            renderer.nativeNextDebugAnimation()
        }

        setupCashCalibrationPanel()
    }

    private fun setupCashCalibrationPanel() {
        val panel = findViewById<android.view.View>(R.id.cash_calibration_panel)
        val toggle = findViewById<Button>(R.id.btn_adjust)
        val reset = findViewById<Button>(R.id.btn_reset_adjust)
        val copy = findViewById<Button>(R.id.btn_copy_adjust)

        val labels = arrayOf(
            findViewById<TextView>(R.id.lbl_pos_x),
            findViewById<TextView>(R.id.lbl_pos_y),
            findViewById<TextView>(R.id.lbl_pos_z),
            findViewById<TextView>(R.id.lbl_rot_x),
            findViewById<TextView>(R.id.lbl_rot_y),
            findViewById<TextView>(R.id.lbl_rot_z)
        )

        val bars = arrayOf(
            findViewById<SeekBar>(R.id.seek_pos_x),
            findViewById<SeekBar>(R.id.seek_pos_y),
            findViewById<SeekBar>(R.id.seek_pos_z),
            findViewById<SeekBar>(R.id.seek_rot_x),
            findViewById<SeekBar>(R.id.seek_rot_y),
            findViewById<SeekBar>(R.id.seek_rot_z)
        )

        var posX = 0f
        var posY = 1f
        var posZ = 0f
        var rotX = 0f
        var rotY = -91f
        var rotZ = 180f

        fun apply() {
            renderer.nativeSetCashTransform(
                posX, posY, posZ,
                rotX, rotY, rotZ
            )

            labels[0].text = "Pos X: %+.2f m".format(posX)
            labels[1].text = "Pos Y: %+.2f m".format(posY)
            labels[2].text = "Pos Z: %+.2f m".format(posZ)
            labels[3].text = "Rot X: %+.0f°".format(rotX)
            labels[4].text = "Rot Y: %+.0f°".format(rotY)
            labels[5].text = "Rot Z: %+.0f°".format(rotZ)
        }

        fun refreshFromBars() {
            posX = (bars[0].progress - 300) / 100f
            posY = (bars[1].progress - 300) / 100f
            posZ = (bars[2].progress - 300) / 100f
            rotX = (bars[3].progress - 180).toFloat()
            rotY = (bars[4].progress - 180).toFloat()
            rotZ = (bars[5].progress - 180).toFloat()
            apply()
        }

        bars.forEach { bar ->
            bar.setOnSeekBarChangeListener(
                simpleSeekListener { refreshFromBars() }
            )
        }

        toggle.setOnClickListener {
            panel.visibility =
                if (panel.visibility == android.view.View.VISIBLE)
                    android.view.View.GONE
                else
                    android.view.View.VISIBLE
        }

        reset.setOnClickListener {
            bars.forEachIndexed { index, bar ->
                bar.progress = when (index) {
                    0, 1, 2, 3 -> if (index < 3) 300 else 180
                    4 -> 89
                    5 -> 360
                    else -> 180
                }
            }
            refreshFromBars()
        }

        copy.setOnClickListener {
            val values =
                "Pos X=${"%.2f".format(posX)}, " +
                "Pos Y=${"%.2f".format(posY)}, " +
                "Pos Z=${"%.2f".format(posZ)}, " +
                "Rot X=${"%.0f".format(rotX)}°, " +
                "Rot Y=${"%.0f".format(rotY)}°, " +
                "Rot Z=${"%.0f".format(rotZ)}°"
            val clipboard =
                getSystemService(CLIPBOARD_SERVICE) as ClipboardManager
            clipboard.setPrimaryClip(
                ClipData.newPlainText("Cash transform", values)
            )
            Toast.makeText(this, "Valores copiados", Toast.LENGTH_SHORT).show()
        }

        // Valores finales de calibración.
        bars[0].progress = 300
        bars[1].progress = 400
        bars[2].progress = 300
        bars[3].progress = 180
        bars[4].progress = 89
        bars[5].progress = 360
        refreshFromBars()
    }

    private fun simpleSeekListener(onChanged: () -> Unit) =
        object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(
                seekBar: SeekBar?,
                progress: Int,
                fromUser: Boolean
            ) = onChanged()

            override fun onStartTrackingTouch(seekBar: SeekBar?) = Unit
            override fun onStopTrackingTouch(seekBar: SeekBar?) = Unit
        }

    override fun onResume() { super.onResume(); glView.onResume() }
    override fun onPause()  { super.onPause();  glView.onPause()  }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemUI()
    }

    private fun hideSystemUI() {
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.R) {
            androidx.core.view.WindowCompat.setDecorFitsSystemWindows(window, false)
            window.insetsController?.let {
                it.hide(android.view.WindowInsets.Type.statusBars() or android.view.WindowInsets.Type.navigationBars())
                it.systemBarsBehavior = android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility = (
                android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                or android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                or android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                or android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or android.view.View.SYSTEM_UI_FLAG_FULLSCREEN
            )
        }
    }
}