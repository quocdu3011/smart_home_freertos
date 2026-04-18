package com.quocdu.smarthomemqtt

object MqttConfig {
    const val brokerHost: String = "broker.hivemq.com"
    const val brokerPort: Int = 1883
    const val username: String = ""
    const val password: String = ""

    const val topicBase: String = "smarthome/esp32-devkit"
    const val topicCommand: String = "$topicBase/cmd"
    const val topicState: String = "$topicBase/state"
    const val topicRfidList: String = "$topicBase/rfid/list"
    const val topicEvent: String = "$topicBase/event"
    const val topicAvailability: String = "$topicBase/availability"

    val serverUri: String = "tcp://$brokerHost:$brokerPort"
}
