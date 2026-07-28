package com.example.qwen_echo.engine

import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.util.Log

/// Monitors device thermal state using Android PowerManager.
///
/// Posts EchoMessage.thermalState via MessageStream so the Flutter UI can
/// display warnings. Also exposes the current thermal state for pipeline
/// stages to adapt their behaviour (e.g. reduce LLM context window).
///
/// Mapping (matches iOS ThermalMonitor):
///   0 = Normal (NOMINAL / LIGHT / MODERATE)
///   1 = Throttle (SEVERE)
///   2 = Critical (CRITICAL / SHUTDOWN)
class ThermalMonitor(
    private val context: Context,
    private val messages: MessageStream,
) {
    private val powerManager = context.getSystemService(Context.POWER_SERVICE) as PowerManager
    private val handler = Handler(Looper.getMainLooper())

    /// Current thermal state: 0=Normal, 1=Throttle, 2=Critical
    var currentMode: Int = 0
        private set

    private var isPolling = false
    private val pollRunnable = object : Runnable {
        override fun run() {
            if (!isPolling) return
            handleThermalChange()
            handler.postDelayed(this, 10_000) // poll every 10s
        }
    }

    private val thermalListener = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
        PowerManager.OnThermalStatusChangedListener { status ->
            val mode = mapThermalStatus(status)
            if (mode != currentMode) {
                currentMode = mode
                postCurrentState()
            }
        }
    } else null

    companion object {
        private const val TAG = "ThermalMonitor"
    }

    /// Start observing thermal state changes.
    fun start() {
        postCurrentState()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            // API 29+: use event listener
            thermalListener?.let {
                powerManager.addThermalStatusListener(it)
                Log.d(TAG, "Started (listener, API 29+)")
            }
        } else {
            // API 24-28: poll every 10s
            isPolling = true
            handler.post(pollRunnable)
            Log.d(TAG, "Started (polling, API ${Build.VERSION.SDK_INT})")
        }
    }

    /// Stop observing and clean up.
    fun stop() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            thermalListener?.let {
                powerManager.removeThermalStatusListener(it)
            }
        }
        isPolling = false
        handler.removeCallbacks(pollRunnable)
        Log.d(TAG, "Stopped")
    }

    private fun handleThermalChange() {
        val status = powerManager.currentThermalStatus
        val mode = mapThermalStatus(status)
        if (mode != currentMode) {
            currentMode = mode
            postCurrentState()
        }
    }

    private fun postCurrentState() {
        val detail = when (currentMode) {
            0 -> "Thermal nominal"
            1 -> "Thermal throttle — reducing performance"
            2 -> "Thermal critical — aggressive throttling"
            else -> "Unknown thermal state"
        }
        messages.post(EchoMessage.thermalState(currentMode, detail))
    }

    /// Map Android PowerManager thermal status to the UI integer code.
    /// 0 = Normal, 1 = Throttle, 2 = Critical.
    private fun mapThermalStatus(status: Int): Int {
        return when (status) {
            // PowerManager.THERMAL_STATUS_*
            0, // NONE
            1, // LIGHT
            2, // MODERATE
            -> 0 // Normal
            3 -> 1 // SEVERE → Throttle
            4, // CRITICAL
            5, // EMERGENCY
            6, // SHUTDOWN
            -> 2 // Critical
            else -> 0
        }
    }
}
