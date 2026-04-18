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
    private val username = MutableStateFlow(DEFAULT_USERNAME)
    private val password = MutableStateFlow(DEFAULT_PASSWORD)
    private val isLoggedIn = MutableStateFlow(false)
    private val loginError = MutableStateFlow<String?>(null)

    private data class CombinedRepositoryState(
        val brokerStatus: BrokerStatus,
        val brokerMessage: String,
        val device: DeviceSnapshot,
        val cards: List<String>,
        val events: List<String>
    )

    private data class LoginUiState(
        val username: String,
        val password: String,
        val isLoggedIn: Boolean,
        val loginError: String?
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

    private val loginUiState = combine(username, password, isLoggedIn, loginError) { username, password, isLoggedIn, loginError ->
        LoginUiState(
            username = username,
            password = password,
            isLoggedIn = isLoggedIn,
            loginError = loginError
        )
    }

    val uiState: StateFlow<SmartHomeUiState> = combine(repositoryState, rfidInput, loginUiState) { repositoryState, currentInput, loginUiState ->
        SmartHomeUiState(
            brokerStatus = repositoryState.brokerStatus,
            brokerMessage = repositoryState.brokerMessage,
            device = repositoryState.device,
            rfidInput = currentInput,
            cards = repositoryState.cards,
            events = repositoryState.events,
            username = loginUiState.username,
            password = loginUiState.password,
            isLoggedIn = loginUiState.isLoggedIn,
            loginError = loginUiState.loginError
        )
    }.stateIn(
        scope = viewModelScope,
        started = SharingStarted.WhileSubscribed(5_000),
        initialValue = SmartHomeUiState()
    )

    fun updateUsername(text: String) {
        username.value = text
        loginError.value = null
    }

    fun updatePassword(text: String) {
        password.value = text
        loginError.value = null
    }

    fun login() {
        val currentUsername = username.value.trim()
        val currentPassword = password.value

        if (currentUsername == DEFAULT_USERNAME && currentPassword == DEFAULT_PASSWORD) {
            loginError.value = null
            isLoggedIn.value = true
            repository.connect()
            refreshAll()
            return
        }

        loginError.value = "Sai tài khoản hoặc mật khẩu. Dùng admin / 123456."
    }

    fun logout() {
        isLoggedIn.value = false
        loginError.value = null
        repository.disconnect()
    }

    fun reconnect() {
        if (isLoggedIn.value) {
            repository.connect()
        }
    }

    fun refreshAll() {
        if (isLoggedIn.value) {
            repository.refreshState()
            repository.refreshRfidList()
        }
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

    private companion object {
        const val DEFAULT_USERNAME = "admin"
        const val DEFAULT_PASSWORD = "123456"
    }
}
