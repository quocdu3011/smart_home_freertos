package com.quocdu.smarthomemqtt

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update

class SmartHomeViewModel(application: Application) : AndroidViewModel(application) {
    private val repository = MqttRepository()
    private val rfidInput = MutableStateFlow("")

    private data class CombinedRepositoryState(
        val brokerStatus: BrokerStatus,
        val brokerMessage: String,
        val device: DeviceSnapshot,
        val cards: List<String>,
        val events: List<String>
    )

    private val repositoryState = combine(
        repository.brokerStatus,
        repository.brokerMessage,
        repository.deviceSnapshot,
        repository.cards,
        repository.events
    ) { brokerStatus, brokerMessage, device, cards, events ->
        CombinedRepositoryState(
            brokerStatus = brokerStatus,
            brokerMessage = brokerMessage,
            device = device,
            cards = cards,
            events = events
        )
    }

    val uiState: StateFlow<SmartHomeUiState> = combine(repositoryState, rfidInput) { repositoryState, currentInput ->
        SmartHomeUiState(
            brokerStatus = repositoryState.brokerStatus,
            brokerMessage = repositoryState.brokerMessage,
            device = repositoryState.device,
            rfidInput = currentInput,
            cards = repositoryState.cards,
            events = repositoryState.events
        )
    }.stateIn(
        scope = viewModelScope,
        started = SharingStarted.WhileSubscribed(5_000),
        initialValue = SmartHomeUiState()
    )

    init {
        repository.connect()
    }

    fun reconnect() = repository.connect()

    fun refreshAll() {
        repository.refreshState()
        repository.refreshRfidList()
    }

    fun openGate() = repository.openGate()

    fun openEntryDoor() = repository.openEntryDoor()

    fun closeEntryDoor() = repository.closeEntryDoor()

    fun toggleEntryDoor() = repository.toggleEntryDoor()

    fun updateRfidInput(text: String) {
        rfidInput.value = text.uppercase()
    }

    fun addRfidCard() {
        val uid = rfidInput.value.trim()
        if (uid.isEmpty()) {
            return
        }

        repository.addRfidCard(uid)
        rfidInput.update { "" }
    }

    fun removeRfidCard(uidText: String) = repository.removeRfidCard(uidText)

    override fun onCleared() {
        repository.disconnect()
        super.onCleared()
    }
}
