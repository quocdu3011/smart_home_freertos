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

    private val _brokerMessage = MutableStateFlow("Chưa kết nối")
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
        _brokerMessage.value = "Đang kết nối tới ${MqttConfig.brokerHost}:${MqttConfig.brokerPort}"

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
                _brokerMessage.value = "MQTT đã kết nối"
                subscribeToTopics()
                requestSnapshot()
                pushEvent("Đã kết nối broker MQTT.")
            }.onFailure { throwable ->
                _brokerStatus.value = BrokerStatus.ERROR
                _brokerMessage.value = throwable.message ?: "Khởi tạo MQTT thất bại"
                pushEvent("Khởi tạo MQTT thất bại.")
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
            _brokerMessage.value = "Đã ngắt kết nối"
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
                pushEvent("MQTT chưa kết nối.")
                return@launch
            }

            runCatching {
                val message = MqttMessage(jsonObject.toString().toByteArray()).apply {
                    qos = 1
                    isRetained = false
                }
                client.publish(MqttConfig.topicCommand, message)
            }.onFailure { throwable ->
                pushEvent("Gửi lệnh thất bại.")
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
            pushEvent("Đăng ký topic thất bại.")
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
            _brokerMessage.value = if (reconnect) "MQTT đã kết nối lại" else "MQTT đã kết nối"
            subscribeToTopics()
            requestSnapshot()
            pushEvent(if (reconnect) "Đã kết nối lại broker MQTT." else "Đã kết nối broker MQTT.")
        }

        override fun connectionLost(cause: Throwable?) {
            _brokerStatus.value = BrokerStatus.ERROR
            _brokerMessage.value = cause?.message ?: "Mất kết nối MQTT"
            _deviceSnapshot.update { it.copy(availability = "offline") }
            pushEvent("Mất kết nối MQTT.")
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
            pushEvent("Đọc trạng thái thất bại.")
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
            pushEvent("Đọc danh sách RFID thất bại.")
            Log.e("MqttRepository", "rfid parse failure", throwable)
        }
    }

    private fun handleEventMessage(payload: String) {
        runCatching {
            val json = JSONObject(payload)
            val type = json.optString("type", "event")
            val message = json.optString("message", "")
            val uid = json.optString("uid", "")
            val text = listOf(translateDeviceType(type), translateDeviceMessage(message), uid)
                .filter { it.isNotBlank() }
                .joinToString(" | ")
            pushEvent(text)
        }.onFailure { throwable ->
            pushEvent("Đọc sự kiện thất bại.")
            Log.e("MqttRepository", "event parse failure", throwable)
        }
    }

    private fun handleAvailabilityMessage(payload: String) {
        val availability = payload.trim().ifBlank { "offline" }
        _deviceSnapshot.update { it.copy(availability = availability) }
        pushEvent("Trạng thái thiết bị: ${translateAvailability(availability)}")
    }

    private fun pushEvent(message: String) {
        val timestamp = java.text.SimpleDateFormat("HH:mm:ss", Locale.getDefault())
            .format(java.util.Date())
        _events.update { current ->
            listOf("$timestamp  $message") + current
        }
        _events.update { it.take(8) }
    }

    private fun translateDeviceType(type: String): String {
        return when (type) {
            "rfid_add" -> "Thêm RFID"
            "rfid_remove" -> "Xóa RFID"
            "rfid_granted" -> "RFID hợp lệ"
            "rfid_denied" -> "RFID bị từ chối"
            "gate_open" -> "Mở cổng"
            "entry_open" -> "Mở cửa"
            "entry_close" -> "Đóng cửa"
            "entry_toggle" -> "Đảo trạng thái cửa"
            "mqtt_connected" -> "MQTT"
            "wifi_connected" -> "Wi-Fi"
            "wifi_portal" -> "Cấu hình Wi-Fi"
            "dht_failure" -> "Cảm biến DHT11"
            else -> type
        }
    }

    private fun translateDeviceMessage(message: String): String {
        return when (message) {
            "RFID card added." -> "Đã thêm thẻ RFID."
            "RFID card removed." -> "Đã xóa thẻ RFID."
            "Invalid RFID UID format." -> "UID RFID không hợp lệ."
            "RFID card already exists." -> "Thẻ RFID đã tồn tại."
            "RFID card not found." -> "Không tìm thấy thẻ RFID."
            "RFID list is full." -> "Danh sách RFID đã đầy."
            "Failed to save RFID list." -> "Lưu danh sách RFID thất bại."
            "Authorized RFID card accepted." -> "Thẻ RFID hợp lệ."
            "Unauthorized RFID card rejected." -> "Thẻ RFID không hợp lệ."
            "Gate open command received from app." -> "Đã nhận lệnh mở cổng từ app."
            "Entry door open command received from app." -> "Đã nhận lệnh mở cửa từ app."
            "Entry door close command received from app." -> "Đã nhận lệnh đóng cửa từ app."
            "Entry door toggle command received from app." -> "Đã nhận lệnh đổi trạng thái cửa từ app."
            "WiFi config portal is active." -> "Trang cấu hình Wi-Fi đang mở."
            "WiFi connected." -> "Wi-Fi đã kết nối."
            "DHT11 failed repeatedly." -> "DHT11 lỗi lặp lại."
            else -> message
        }
    }

    private fun translateAvailability(availability: String): String {
        return if (availability.equals("online", ignoreCase = true)) {
            "trực tuyến"
        } else {
            "ngoại tuyến"
        }
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
