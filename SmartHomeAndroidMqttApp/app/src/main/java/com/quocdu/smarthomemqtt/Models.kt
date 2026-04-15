package com.quocdu.smarthomemqtt

enum class BrokerStatus {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    ERROR
}

data class DeviceSnapshot(
    val deviceId: String = "",
    val availability: String = "offline",
    val wifiConnected: Boolean = false,
    val mqttConnected: Boolean = false,
    val wifiPortalActive: Boolean = false,
    val ipAddress: String = "",
    val gateOpen: Boolean = false,
    val entryDoorOpen: Boolean = false,
    val climateValid: Boolean = false,
    val temperatureC: Double? = null,
    val humidityPct: Double? = null,
    val dhtFailureCount: Int = 0,
    val authorizedCardCount: Int = 0,
    val lastRfidUid: String = "",
    val lastAccessGranted: Boolean = false,
    val lastEvent: String = "",
    val uptimeMs: Long = 0L
)

data class SmartHomeUiState(
    val brokerStatus: BrokerStatus = BrokerStatus.DISCONNECTED,
    val brokerMessage: String = "Disconnected",
    val device: DeviceSnapshot = DeviceSnapshot(),
    val rfidInput: String = "",
    val cards: List<String> = emptyList(),
    val events: List<String> = emptyList()
)
