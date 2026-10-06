package com.manhunt.port

import android.content.res.AssetManager
import android.opengl.GLSurfaceView
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class ManhuntRenderer(private val assets: AssetManager, private val gamePath: String) : GLSurfaceView.Renderer {

    init {
        nativeInit(assets, gamePath)
    }

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        nativeSurfaceCreated()
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        nativeSurfaceChanged(width, height)
    }

    override fun onDrawFrame(gl: GL10?) {
        nativeDrawFrame()
    }

    private external fun nativeInit(assetManager: AssetManager, gamePath: String)
    private external fun nativeSurfaceCreated()
    private external fun nativeSurfaceChanged(width: Int, height: Int)
    private external fun nativeDrawFrame()

    external fun nativeMove(fwd: Float, right: Float)
    external fun nativeLook(dx: Float, dy: Float, touchMode: Boolean)
    external fun nativeJump()
    external fun nativeNextDebugAnimation()
    external fun nativeSetCashTransform(
        posX: Float,
        posY: Float,
        posZ: Float,
        rotX: Float,
        rotY: Float,
        rotZ: Float
    )

    companion object {
        init { System.loadLibrary("manhunt") }
    }
}
