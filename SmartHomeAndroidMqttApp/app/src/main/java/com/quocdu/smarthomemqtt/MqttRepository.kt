package com.quocdu.smarthomemqtt

import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import org.eclipse.paho.client.mqttv3.IMqttDeliveryToken
import org.eclipse.paho.client.mqttv3.MqttCallbackExtended
import org.eclipse.paho.client.mqttv3.MqttClient
import org.eclipse.paho.client.mqttv3.MqttConnectOptions
import org.eclipse.paho.client.mqttv3.MqttMessage
import org.eclipse.paho.client.mqttv3.persist.MemoryPersistence
import org.json.JSONArray
import org.json.JSONObject
import java.util.Locale
import java.util.UUID

class MqttRepository {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var mqttClient: MqttClient? = null

    private val _brokerStatus = MutableStateFlow(BrokerStatus.DISCONNECTED)
    val brokerStatus: StateFlow<BrokerStatus> = _brokerStatus.asStateFlow()

    private val _brokerMessage = MutableStateFlow("Disconnected")
    val brokerMessage: StateFlow<String> = _brokerMessage.asStateFlow()

    private val _deviceSnapshot = MutableStateFlow(DeviceSnapshot())
    val deviceSnapshot: StateFlow<DeviceSnapshot> = _deviceSnapshot.asStateFlow()

    private val _cards = MutableStateFlow<List<String>>(emptyList())
    val cards: StateFlow<List<String>> = _cards.asStateFlow()

    private val _events = MutableStateFlow<List<String>>(emptyList())
    val events: StateFlow<List<String>> = _events.asStateFlow()

    fun connect() {
        if (mqttClient?.isConnected == true || _brokerStatus.value == BrokerStatus.CONNECTING) {
            return
        }

        _brokerStatus.value = BrokerStatus.CONNECTING
        _brokerMessage.value = "Connecting to ${MqttConfig.brokerHost}:${MqttConfig.brokerPort}"

        scope.launch {
            runCatching {
                if (mqttClient == null) {
                    mqttClient = MqttClient(
                        MqttConfig.serverUri,
                        "android-${UUID.randomUUID()}",
                        MemoryPersistence()
                    ).apply {
                        setCallback(mqttCallback)
                    }
                }

                val options = MqttConnectOptions().apply {
                    isAutomaticReconnect = true
                    isCleanSession = false
                    connectionTimeout = 15
                    keepAliveInterval = 20

                    if (MqttConfig.username.isNotBlank()) {
                        userName = MqttConfig.username
                        password = MqttConfig.password.toCharArray()
                    }
                }

                mqttClient?.connect(options)
                _brokerStatus.value = BrokerStatus.CONNECTED
                _brokerMessage.value = "MQTT connected"
                subscribeToTopics()
                requestSnapshot()
                pushEvent("Connected to MQTT broker.")
            }.onFailure { throwable ->
                _brokerStatus.value = BrokerStatus.ERROR
                _brokerMessage.value = throwable.message ?: "MQTT setup failed"
                pushEvent("MQTT setup failed.")
                Log.e("MqttRepository", "connect failure", throwable)
            }
        }
    }

    fun disconnect() {
        scope.launch {
            runCatching {
                mqttClient?.disconnect()
                mqttClient?.close()
            }
            mqttClient = null
            _brokerStatus.value = BrokerStatus.DISCONNECTED
            _brokerMessage.value = "Disconnected"
        }
    }

    fun openGate() = publishCommand(JSONObject().put("action", "gate_open"))

    fun openEntryDoor() = publishCommand(JSONObject().put("action", "entry_open"))

    fun closeEntryDoor() = publishCommand(JSONObject().put("action", "entry_close"))

    fun toggleEntryDoor() = publishCommand(JSONObject().put("action", "entry_toggle"))

    fun refreshState() = publishCommand(JSONObject().put("action", "state_get"))

    fun refreshRfidList() = publishCommand(JSONObject().put("action", "rfid_list"))

    fun addRfidCard(uidText: String) {
        publishCommand(
            JSONObject()
                .put("action", "rfid_add")
                .put("uid", uidText)
        )
    }

    fun removeRfidCard(uidText: String) {
        publishCommand(
            JSONObject()
                .put("action", "rfid_remove")
                .put("uid", uidText)
        )
    }

    private fun publishCommand(jsonObject: JSONObject) {
        scope.launch {
            val client = mqttClient
            if (client == null || !client.isConnected) {
                pushEvent("MQTT is not connected.")
                return@launch
            }

            runCatching {
                val message = MqttMessage(jsonObject.toString().toByteArray()).apply {
                    qos = 1
                    isRetained = false
                }
                client.publish(MqttConfig.topicCommand, message)
            }.onFailure { throwable ->
                pushEvent("Failed to publish command.")
                Log.e("MqttRepository", "publish failure", throwable)
            }
        }
    }

    private fun subscribeToTopics() {
        runCatching {
            mqttClient?.subscribe(MqttConfig.topicState, 1)
            mqttClient?.subscribe(MqttConfig.topicRfidList, 1)
            mqttClient?.subscribe(MqttConfig.topicEvent, 1)
            mqttClient?.subscribe(MqttConfig.topicAvailability, 1)
        }.onFailure { throwable ->
            pushEvent("Subscribe failed.")
            Log.e("MqttRepository", "subscribe failure", throwable)
        }
    }

    private fun requestSnapshot() {
        refreshState()
        refreshRfidList()
    }

    private val mqttCallback = object : MqttCallbackExtended {
        override fun connectComplete(reconnect: Boolean, serverURI: String?) {
            _brokerStatus.value = BrokerStatus.CONNECTED
            _brokerMessage.value = if (reconnect) "MQTT reconnected" else "MQTT connected"
            subscribeToTopics()
            requestSnapshot()
            pushEvent(if (reconnect) "Reconnected to MQTT broker." else "Connected to MQTT broker.")
        }

        override fun connectionLost(cause: Throwable?) {
            _brokerStatus.value = BrokerStatus.ERROR
            _brokerMessage.value = cause?.message ?: "MQTT connection lost"
            _deviceSnapshot.update { it.copy(availability = "offline") }
            pushEvent("MQTT connection lost.")
        }

        override fun messageArrived(topic: String?, message: MqttMessage?) {
            if (topic == null || message == null) {
                return
            }

            val payload = message.toString()

            when (topic) {
                MqttConfig.topicState -> handleStateMessage(payload)
                MqttConfig.topicRfidList -> handleRfidListMessage(payload)
                MqttConfig.topicEvent -> handleEventMessage(payload)
                MqttConfig.topicAvailability -> handleAvailabilityMessage(payload)
            }
        }

        override fun deliveryComplete(token: IMqttDeliveryToken?) = Unit
    }

    private fun handleStateMessage(payload: String) {
        runCatching {
            val json = JSONObject(payload)
            _deviceSnapshot.value = DeviceSnapshot(
                deviceId = json.optString("deviceId", ""),
                availability = _deviceSnapshot.value.availability,
                wifiConnected = json.optBoolean("wifiConnected", false),
                mqttConnected = json.optBoolean("mqttConnected", false),
                wifiPortalActive = json.optBoolean("wifiPortalActive", false),
                ipAddress = json.optString("ip", ""),
                gateOpen = json.optBoolean("gateOpen", false),
                entryDoorOpen = json.optBoolean("entryDoorOpen", false),
                climateValid = json.optBoolean("climateValid", false),
                temperatureC = json.optNullableDouble("temperatureC"),
                humidityPct = json.optNullableDouble("humidityPct"),
                dhtFailureCount = json.optInt("dhtFailureCount", 0),
                authorizedCardCount = json.optInt("authorizedCardCount", 0),
                lastRfidUid = json.optString("lastRfidUid", ""),
                lastAccessGranted = json.optBoolean("lastAccessGranted", false),
                lastEvent = json.optString("lastEvent", ""),
                uptimeMs = json.optLong("uptimeMs", 0L)
            )
        }.onFailure { throwable ->
            pushEvent("State parse failed.")
            Log.e("MqttRepository", "state parse failure", throwable)
        }
    }

    private fun handleRfidListMessage(payload: String) {
        runCatching {
            val json = JSONObject(payload)
            val cardsArray = json.optJSONArray("cards") ?: JSONArray()
            val cards = buildList {
                for (index in 0 until cardsArray.length()) {
                    add(cardsArray.optString(index))
                }
            }.filter { it.isNotBlank() }

            _cards.value = cards
        }.onFailure { throwable ->
            pushEvent("RFID list parse failed.")
            Log.e("MqttRepository", "rfid parse failure", throwable)
        }
    }

    private fun handleEventMessage(payload: String) {
        runCatching {
            val json = JSONObject(payload)
            val type = json.optString("type", "event")
            val message = json.optString("message", "")
            val uid = json.optString("uid", "")
            val text = listOf(type, message, uid)
                .filter { it.isNotBlank() }
                .joinToString(" | ")
            pushEvent(text)
        }.onFailure { throwable ->
            pushEvent("Event parse failed.")
            Log.e("MqttRepository", "event parse failure", throwable)
        }
    }

    private fun handleAvailabilityMessage(payload: String) {
        val availability = payload.trim().ifBlank { "offline" }
        _deviceSnapshot.update { it.copy(availability = availability) }
        pushEvent("Device availability: $availability")
    }

    private fun pushEvent(message: String) {
        val timestamp = java.text.SimpleDateFormat("HH:mm:ss", Locale.getDefault())
            .format(java.util.Date())
        _events.update { current ->
            listOf("$timestamp  $message") + current
        }
        _events.update { it.take(8) }
    }
}

private fun JSONObject.optNullableDouble(key: String): Double? {
    if (isNull(key) || !has(key)) {
        return null
    }

    val value = opt(key)
    return when (value) {
        is Number -> value.toDouble()
        is String -> value.toDoubleOrNull()
        else -> null
    }
}
