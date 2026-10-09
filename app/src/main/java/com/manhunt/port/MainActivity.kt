package com.manhunt.port

import android.app.Activity
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.view.MotionEvent
import android.view.Gravity
import android.view.ViewGroup
import android.graphics.Color
import android.util.TypedValue
import android.widget.FrameLayout
import android.widget.LinearLayout
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
    private var rightLastX = 0f
    private var rightLastY = 0f
    // Sensibilidad del arrastre táctil: píxeles de desplazamiento para alcanzar
    // la velocidad máxima de la zona de apuntado.
    private var touchAimDistance = 14f

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
                            rightLastX     = px
                            rightLastY     = py
                            renderer.nativeLook(0f, 0f, true)
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
                                    // Apuntado táctil: usamos el desplazamiento desde el último
                                    // evento, no una posición fija desde donde comenzó el toque.
                                    val dx = cx - rightLastX
                                    val dy = cy - rightLastY
                                    rightLastX = cx
                                    rightLastY = cy

                                    val nx = (dx / touchAimDistance).coerceIn(-1f, 1f)
                                    val ny = (dy / touchAimDistance).coerceIn(-1f, 1f)

                                    renderer.nativeLook(nx, ny, true)
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
                                renderer.nativeLook(0f, 0f, true)
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

        // Panel de diagnóstico dentro del panel AJUSTE. Las columnas tienen
        // anchos fijos para evitar solapamientos en landscape.
        val lightingLabels = arrayOf(
            "Debug: prelit",
            "Debug: prelit × material",
            "Debug: textura sola",
            "Debug: prelit boosted",
            "EXPERIMENTAL: luz Cash",
            "Debug: textura × prelit"
        )
        var lightingMode = 0
        renderer.nativeSetLightingDebugMode(lightingMode)
        val density = resources.displayMetrics.density
        val safeMargin = (18 * density).toInt()
        val panel = findViewById<LinearLayout>(R.id.cash_calibration_panel)
        val toggle = findViewById<Button>(R.id.btn_adjust)

        val lightingButton = Button(this).apply {
            text = lightingLabels[lightingMode]
            isAllCaps = false
            setTextColor(Color.WHITE)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            setPadding((6 * density).toInt(), 0, (6 * density).toInt(), 0)
            setBackgroundColor(Color.argb(210, 0, 0, 0))
            setOnClickListener {
                lightingMode = (lightingMode + 1) % lightingLabels.size
                renderer.nativeSetLightingDebugMode(lightingMode)
                text = lightingLabels[lightingMode]
            }
        }
        val lightingButtonParams = FrameLayout.LayoutParams(
            (190 * density).toInt(),
            (42 * density).toInt(),
            Gravity.TOP or Gravity.END
        )
        lightingButtonParams.topMargin = safeMargin
        lightingButtonParams.marginEnd = safeMargin
        container.addView(lightingButton, lightingButtonParams)

        val debugControls = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding((4 * density).toInt(), (4 * density).toInt(),
                (4 * density).toInt(), (4 * density).toInt())
        }
        fun addDebugAdjustment(label: String, setting: Int) {
            val row = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                setPadding(0, 0, 0, (2 * density).toInt())
            }
            val caption = TextView(this).apply {
                text = label
                setTextColor(Color.WHITE)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
                gravity = Gravity.CENTER_VERTICAL
                maxLines = 1
            }
            row.addView(caption, LinearLayout.LayoutParams(
                0, (36 * density).toInt(), 1f
            ))
            fun stepButton(symbol: String, direction: Int) = Button(this).apply {
                text = symbol
                isAllCaps = false
                setTextColor(Color.WHITE)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
                setPadding(0, 0, 0, 0)
                minWidth = 0
                setBackgroundColor(Color.argb(220, 45, 45, 45))
                setOnClickListener {
                    renderer.nativeAdjustRenderDebug(setting, direction)
                    value.text = "%.2f".format(java.util.Locale.US,
                        renderer.nativeGetRenderDebugValue(setting))
                }
            }
            val minus = stepButton("−", -1)
            row.addView(minus, LinearLayout.LayoutParams(
                (36 * density).toInt(), (34 * density).toInt()
            ))
            val value = TextView(this).apply {
                text = "%.2f".format(java.util.Locale.US,
                    renderer.nativeGetRenderDebugValue(setting))
                setTextColor(Color.WHITE)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
                gravity = Gravity.CENTER
                maxLines = 1
            }
            row.addView(value, LinearLayout.LayoutParams(
                (48 * density).toInt(), (34 * density).toInt()
            ))
            val plus = stepButton("+", 1)
            row.addView(plus, LinearLayout.LayoutParams(
                (36 * density).toInt(), (34 * density).toInt()
            ))
            debugControls.addView(row, LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
            ))
        }
        addDebugAdjustment("Prelit ×", 0)
        addDebugAdjustment("Gamma", 1)
        addDebugAdjustment("Cash ambiente*", 2)
        addDebugAdjustment("Cash direccional*", 3)
        debugControls.addView(TextView(this).apply {
            text = "* Solo modo experimental de Cash; no es la fórmula original."
            setTextColor(Color.LTGRAY)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 9f)
        })
        panel.addView(debugControls)

        // Mantener AJUSTE fuera del panel y aplicar el margen seguro del notch
        // a los elementos flotantes sin desplazar la superficie 3D.
        androidx.core.view.ViewCompat.setOnApplyWindowInsetsListener(container) { _, insets ->
            val cutout = insets.displayCutout
            val leftSafe = maxOf(safeMargin, cutout?.safeInsetLeft ?: 0)
            val rightSafe = maxOf(safeMargin, cutout?.safeInsetRight ?: 0)
            val topSafe = maxOf(safeMargin, cutout?.safeInsetTop ?: 0)
            (toggle.layoutParams as? FrameLayout.LayoutParams)?.let { lp ->
                lp.marginStart = leftSafe
                lp.topMargin = topSafe
                toggle.layoutParams = lp
            }
            (panel.layoutParams as? FrameLayout.LayoutParams)?.let { lp ->
                lp.marginStart = leftSafe
                lp.topMargin = topSafe + (toggle.layoutParams.height.takeIf { it > 0 } ?: (54 * density).toInt()) + (8 * density).toInt()
                lp.width = minOf((340 * density).toInt(),
                    (container.width - leftSafe - rightSafe).coerceAtLeast((250 * density).toInt()))
                panel.layoutParams = lp
            }
            (lightingButton.layoutParams as? FrameLayout.LayoutParams)?.let { lp ->
                lp.marginEnd = rightSafe
                lp.topMargin = topSafe
                lightingButton.layoutParams = lp
            }
            insets
        }
        val sprintButton = findViewById<android.widget.Button>(R.id.btn_sprint)
        sprintButton.setOnTouchListener { _, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> renderer.nativeSetSprint(true)
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> renderer.nativeSetSprint(false)
            }
            true
        }

        val sneakButton = findViewById<android.widget.Button>(R.id.btn_sneak)
        sneakButton.setOnTouchListener { _, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> renderer.nativeSetSneak(true)
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> renderer.nativeSetSneak(false)
            }
            true
        }

        setupCashCalibrationPanel()
    }

    private fun setupCashCalibrationPanel() {
        val panel = findViewById<android.view.View>(R.id.cash_calibration_panel)
        val toggle = findViewById<Button>(R.id.btn_adjust)
        val aimSensitivity = findViewById<SeekBar>(R.id.seek_aim_sensitivity)
        val aimSensitivityLabel = findViewById<TextView>(R.id.lbl_aim_sensitivity)

        fun refreshAimSensitivity() {
            val percent = aimSensitivity.progress.coerceIn(0, 100)
            if (percent == 0) {
                touchAimDistance = 60f
            } else {
                touchAimDistance = 60f - (percent * 0.54f)
            }
            renderer.nativeSetTouchSensitivity(percent.toFloat())
            aimSensitivityLabel.text = "Sensibilidad táctil: $percent%"
        }

        aimSensitivity.setOnSeekBarChangeListener(
            simpleSeekListener { refreshAimSensitivity() }
        )
        aimSensitivity.progress = 50
        refreshAimSensitivity()

        toggle.setOnClickListener {
            panel.visibility =
                if (panel.visibility == android.view.View.VISIBLE)
                    android.view.View.GONE
                else
                    android.view.View.VISIBLE
        }
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