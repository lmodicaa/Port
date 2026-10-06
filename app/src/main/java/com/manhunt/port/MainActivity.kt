package com.manhunt.port

import android.app.Activity
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.view.MotionEvent
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
            // Futura acción
        }
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