@file:OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)

package com.quocdu.smarthomemqtt

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CloudDone
import androidx.compose.material.icons.rounded.CreditCard
import androidx.compose.material.icons.rounded.DeleteOutline
import androidx.compose.material.icons.rounded.ExitToApp
import androidx.compose.material.icons.rounded.Garage
import androidx.compose.material.icons.rounded.Lock
import androidx.compose.material.icons.rounded.MeetingRoom
import androidx.compose.material.icons.rounded.Person
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Thermostat
import androidx.compose.material.icons.rounded.WarningAmber
import androidx.compose.material.icons.rounded.Wifi
import androidx.compose.material3.AssistChip
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ElevatedCard
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.quocdu.smarthomemqtt.ui.theme.AppSand
import com.quocdu.smarthomemqtt.ui.theme.Clay
import com.quocdu.smarthomemqtt.ui.theme.DeepSea
import com.quocdu.smarthomemqtt.ui.theme.MintMist
import com.quocdu.smarthomemqtt.ui.theme.SmartHomeMqttTheme
import com.quocdu.smarthomemqtt.ui.theme.SunAccent

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setContent {
            SmartHomeMqttTheme {
                val viewModel: SmartHomeViewModel = viewModel()
                val uiState by viewModel.uiState.collectAsStateWithLifecycle()

                AppScreen(
                    uiState = uiState,
                    onUsernameChanged = viewModel::updateUsername,
                    onPasswordChanged = viewModel::updatePassword,
                    onLogin = viewModel::login,
                    onLogout = viewModel::logout,
                    onReconnect = viewModel::reconnect,
                    onRefresh = viewModel::refreshAll,
                    onOpenGate = viewModel::openGate,
                    onOpenEntryDoor = viewModel::openEntryDoor,
                    onCloseEntryDoor = viewModel::closeEntryDoor,
                    onToggleEntryDoor = viewModel::toggleEntryDoor,
                    onRfidInputChanged = viewModel::updateRfidInput,
                    onAddCard = viewModel::addRfidCard,
                    onRemoveCard = viewModel::removeRfidCard
                )
            }
        }
    }
}

@Composable
private fun AppScreen(
    uiState: SmartHomeUiState,
    onUsernameChanged: (String) -> Unit,
    onPasswordChanged: (String) -> Unit,
    onLogin: () -> Unit,
    onLogout: () -> Unit,
    onReconnect: () -> Unit,
    onRefresh: () -> Unit,
    onOpenGate: () -> Unit,
    onOpenEntryDoor: () -> Unit,
    onCloseEntryDoor: () -> Unit,
    onToggleEntryDoor: () -> Unit,
    onRfidInputChanged: (String) -> Unit,
    onAddCard: () -> Unit,
    onRemoveCard: (String) -> Unit
) {
    if (uiState.isLoggedIn) {
        HomeScreen(
            uiState = uiState,
            onLogout = onLogout,
            onReconnect = onReconnect,
            onRefresh = onRefresh,
            onOpenGate = onOpenGate,
            onOpenEntryDoor = onOpenEntryDoor,
            onCloseEntryDoor = onCloseEntryDoor,
            onToggleEntryDoor = onToggleEntryDoor,
            onRfidInputChanged = onRfidInputChanged,
            onAddCard = onAddCard,
            onRemoveCard = onRemoveCard
        )
    } else {
        LoginScreen(
            uiState = uiState,
            onUsernameChanged = onUsernameChanged,
            onPasswordChanged = onPasswordChanged,
            onLogin = onLogin
        )
    }
}

@Composable
private fun LoginScreen(
    uiState: SmartHomeUiState,
    onUsernameChanged: (String) -> Unit,
    onPasswordChanged: (String) -> Unit,
    onLogin: () -> Unit
) {
    val backgroundBrush = Brush.verticalGradient(
        colors = listOf(Color(0xFFF7EFE7), Color(0xFFE7EFEA))
    )
    val cardBrush = Brush.linearGradient(listOf(DeepSea, Clay, SunAccent))

    Scaffold { innerPadding ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(backgroundBrush)
                .padding(innerPadding)
                .padding(20.dp),
            contentAlignment = Alignment.Center
        ) {
            ElevatedCard(
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(32.dp),
                colors = CardDefaults.elevatedCardColors(containerColor = Color.White.copy(alpha = 0.94f))
            ) {
                Column(
                    modifier = Modifier.padding(20.dp),
                    verticalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    Card(
                        shape = RoundedCornerShape(24.dp),
                        colors = CardDefaults.cardColors(containerColor = Color.Transparent)
                    ) {
                        Column(
                            modifier = Modifier
                                .fillMaxWidth()
                                .background(cardBrush)
                                .padding(18.dp),
                            verticalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            Text(
                                text = "Nhà thông minh MQTT",
                                style = MaterialTheme.typography.headlineSmall,
                                color = Color.White,
                                fontWeight = FontWeight.Bold
                            )
                        }
                    }

                    OutlinedTextField(
                        value = uiState.username,
                        onValueChange = onUsernameChanged,
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = true,
                        shape = RoundedCornerShape(18.dp),
                        label = { Text("Tên đăng nhập") },
                        leadingIcon = {
                            Icon(Icons.Rounded.Person, contentDescription = null)
                        }
                    )

                    OutlinedTextField(
                        value = uiState.password,
                        onValueChange = onPasswordChanged,
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = true,
                        shape = RoundedCornerShape(18.dp),
                        label = { Text("Mật khẩu") },
                        visualTransformation = PasswordVisualTransformation(),
                        leadingIcon = {
                            Icon(Icons.Rounded.Lock, contentDescription = null)
                        }
                    )

                    uiState.loginError?.let { error ->
                        WarningStrip(text = error)
                    }

                    Button(
                        onClick = onLogin,
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(20.dp)
                    ) {
                        Icon(Icons.Rounded.Lock, contentDescription = null)
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("Đăng nhập", fontWeight = FontWeight.SemiBold)
                    }
                }
            }
        }
    }
}

@Composable
private fun HomeScreen(
    uiState: SmartHomeUiState,
    onLogout: () -> Unit,
    onReconnect: () -> Unit,
    onRefresh: () -> Unit,
    onOpenGate: () -> Unit,
    onOpenEntryDoor: () -> Unit,
    onCloseEntryDoor: () -> Unit,
    onToggleEntryDoor: () -> Unit,
    onRfidInputChanged: (String) -> Unit,
    onAddCard: () -> Unit,
    onRemoveCard: (String) -> Unit
) {
    val backgroundBrush = Brush.verticalGradient(
        colors = listOf(Color(0xFFF7EFE7), Color(0xFFE7EFEA))
    )

    Scaffold { innerPadding ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(backgroundBrush)
                .padding(innerPadding)
        ) {
            LazyColumn(
                modifier = Modifier.fillMaxSize(),
                contentPadding = PaddingValues(18.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                item {
                    OverviewCard(
                        uiState = uiState,
                        onReconnect = onReconnect,
                        onRefresh = onRefresh,
                        onLogout = onLogout
                    )
                }

                item {
                    ControlPanel(
                        uiState = uiState,
                        onOpenGate = onOpenGate,
                        onOpenEntryDoor = onOpenEntryDoor,
                        onCloseEntryDoor = onCloseEntryDoor,
                        onToggleEntryDoor = onToggleEntryDoor
                    )
                }

                item {
                    ClimateCard(device = uiState.device)
                }

                item {
                    RfidManagerCard(
                        uiState = uiState,
                        onRfidInputChanged = onRfidInputChanged,
                        onAddCard = onAddCard
                    )
                }

                items(uiState.cards, key = { it }) { uid ->
                    RfidCardRow(
                        uid = uid,
                        onRemove = { onRemoveCard(uid) }
                    )
                }

                item {
                    EventFeedCard(events = uiState.events)
                }
            }
        }
    }
}

@Composable
private fun OverviewCard(
    uiState: SmartHomeUiState,
    onReconnect: () -> Unit,
    onRefresh: () -> Unit,
    onLogout: () -> Unit
) {
    val device = uiState.device
    val gradient = Brush.linearGradient(listOf(DeepSea, Clay, SunAccent))

    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(30.dp),
        colors = CardDefaults.cardColors(containerColor = Color.Transparent)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .background(gradient)
                .padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = "ESP32 cổng thông minh",
                        style = MaterialTheme.typography.headlineSmall,
                        color = Color.White,
                        fontWeight = FontWeight.Bold
                    )
                    Spacer(modifier = Modifier.height(4.dp))
                    Text(
                        text = MqttConfig.topicBase,
                        style = MaterialTheme.typography.bodyMedium,
                        color = Color.White.copy(alpha = 0.82f)
                    )
                }

                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    IconButton(
                        onClick = onRefresh,
                        modifier = Modifier
                            .clip(CircleShape)
                            .background(Color.White.copy(alpha = 0.18f))
                    ) {
                        Icon(
                            imageVector = Icons.Rounded.Refresh,
                            contentDescription = "Làm mới",
                            tint = Color.White
                        )
                    }

                    IconButton(
                        onClick = onLogout,
                        modifier = Modifier
                            .clip(CircleShape)
                            .background(Color.White.copy(alpha = 0.18f))
                    ) {
                        Icon(
                            imageVector = Icons.Rounded.ExitToApp,
                            contentDescription = "Đăng xuất",
                            tint = Color.White
                        )
                    }
                }
            }

            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                StatusChip(
                    label = brokerStatusLabel(uiState.brokerStatus),
                    tint = if (uiState.brokerStatus == BrokerStatus.CONNECTED) MintMist else SunAccent
                )
                StatusChip(
                    label = "Thiết bị ${availabilityLabel(device.availability)}",
                    tint = if (device.availability.equals("online", ignoreCase = true)) MintMist else SunAccent
                )
                StatusChip(
                    label = if (device.wifiPortalActive) {
                        "Portal Wi‑Fi đang mở"
                    } else {
                        "Wi‑Fi ${if (device.wifiConnected) "sẵn sàng" else "ngoại tuyến"}"
                    },
                    tint = if (device.wifiPortalActive) SunAccent else AppSand
                )
                StatusChip(
                    label = "MQTT ${if (device.mqttConnected) "sẵn sàng" else "đang chờ"}",
                    tint = if (device.mqttConnected) MintMist else AppSand
                )
            }

            Text(
                text = uiState.brokerMessage,
                style = MaterialTheme.typography.bodyMedium,
                color = Color.White.copy(alpha = 0.92f)
            )

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column {
                    Text(
                        text = device.deviceId.ifBlank { "Chưa có trạng thái thiết bị" },
                        color = Color.White,
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold
                    )
                    Text(
                        text = if (device.ipAddress.isBlank()) "Đang chờ địa chỉ IP" else device.ipAddress,
                        color = Color.White.copy(alpha = 0.78f),
                        style = MaterialTheme.typography.bodyMedium
                    )
                }

                OutlinedButton(onClick = onReconnect) {
                    Text(text = "Kết nối lại")
                }
            }
        }
    }
}

@Composable
private fun StatusChip(label: String, tint: Color) {
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(999.dp))
            .background(tint.copy(alpha = 0.22f))
            .padding(horizontal = 12.dp, vertical = 8.dp)
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelLarge,
            color = Color.White,
            fontWeight = FontWeight.Medium
        )
    }
}

@Composable
private fun ControlPanel(
    uiState: SmartHomeUiState,
    onOpenGate: () -> Unit,
    onOpenEntryDoor: () -> Unit,
    onCloseEntryDoor: () -> Unit,
    onToggleEntryDoor: () -> Unit
) {
    ElevatedCard(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(28.dp),
        colors = CardDefaults.elevatedCardColors(containerColor = Color.White.copy(alpha = 0.92f))
    ) {
        Column(
            modifier = Modifier.padding(18.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            SectionHeader(title = "Điều khiển", icon = Icons.Rounded.CloudDone)

            Button(
                onClick = onOpenGate,
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(20.dp)
            ) {
                Icon(imageVector = Icons.Rounded.Garage, contentDescription = null)
                Spacer(modifier = Modifier.width(8.dp))
                Text(
                    text = if (uiState.device.gateOpen) "Cổng đang mở" else "Mở cổng trong 5 giây",
                    fontWeight = FontWeight.SemiBold
                )
            }

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                OutlinedButton(
                    onClick = onOpenEntryDoor,
                    modifier = Modifier.weight(1f),
                    shape = RoundedCornerShape(18.dp)
                ) {
                    Icon(Icons.Rounded.MeetingRoom, contentDescription = null)
                    Spacer(modifier = Modifier.width(6.dp))
                    Text("Mở cửa")
                }

                OutlinedButton(
                    onClick = onCloseEntryDoor,
                    modifier = Modifier.weight(1f),
                    shape = RoundedCornerShape(18.dp)
                ) {
                    Icon(Icons.Rounded.MeetingRoom, contentDescription = null)
                    Spacer(modifier = Modifier.width(6.dp))
                    Text("Đóng cửa")
                }
            }

            TextButton(
                onClick = onToggleEntryDoor,
                modifier = Modifier.align(Alignment.End)
            ) {
                Text(
                    text = if (uiState.device.entryDoorOpen) "Chuyển sang đóng" else "Chuyển sang mở",
                    fontWeight = FontWeight.SemiBold
                )
            }
        }
    }
}

@Composable
private fun ClimateCard(device: DeviceSnapshot) {
    ElevatedCard(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(28.dp),
        colors = CardDefaults.elevatedCardColors(containerColor = Color.White.copy(alpha = 0.92f))
    ) {
        Column(
            modifier = Modifier.padding(18.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            SectionHeader(title = "Môi trường", icon = Icons.Rounded.Thermostat)

            if (device.climateValid) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    MetricCard(
                        modifier = Modifier.weight(1f),
                        label = "Nhiệt độ",
                        value = device.temperatureC?.let { String.format("%.1f°C", it) } ?: "--"
                    )
                    MetricCard(
                        modifier = Modifier.weight(1f),
                        label = "Độ ẩm",
                        value = device.humidityPct?.let { String.format("%.1f%%", it) } ?: "--"
                    )
                }
            } else {
                WarningStrip(text = "DHT11 chưa có dữ liệu. Số lần lỗi: ${device.dhtFailureCount}")
            }

            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                AssistChip(
                    onClick = {},
                    label = { Text(if (device.gateOpen) "Cổng mở" else "Cổng đóng") },
                    leadingIcon = { Icon(Icons.Rounded.Garage, contentDescription = null) }
                )
                AssistChip(
                    onClick = {},
                    label = { Text(if (device.entryDoorOpen) "Cửa mở" else "Cửa đóng") },
                    leadingIcon = { Icon(Icons.Rounded.MeetingRoom, contentDescription = null) }
                )
                AssistChip(
                    onClick = {},
                    label = { Text("Thẻ ${device.authorizedCardCount}") },
                    leadingIcon = { Icon(Icons.Rounded.CreditCard, contentDescription = null) }
                )
            }
        }
    }
}

@Composable
private fun MetricCard(
    modifier: Modifier = Modifier,
    label: String,
    value: String
) {
    Box(
        modifier = modifier
            .clip(RoundedCornerShape(22.dp))
            .background(MintMist.copy(alpha = 0.35f))
            .padding(16.dp)
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(
                text = label,
                style = MaterialTheme.typography.labelLarge,
                color = DeepSea.copy(alpha = 0.8f)
            )
            Text(
                text = value,
                style = MaterialTheme.typography.headlineSmall,
                fontWeight = FontWeight.Bold,
                color = DeepSea
            )
        }
    }
}

@Composable
private fun WarningStrip(text: String) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(20.dp))
            .background(SunAccent.copy(alpha = 0.24f))
            .padding(14.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Icon(
            imageVector = Icons.Rounded.WarningAmber,
            contentDescription = null,
            tint = Clay
        )
        Spacer(modifier = Modifier.width(10.dp))
        Text(
            text = text,
            style = MaterialTheme.typography.bodyMedium,
            color = DeepSea
        )
    }
}

@Composable
private fun RfidManagerCard(
    uiState: SmartHomeUiState,
    onRfidInputChanged: (String) -> Unit,
    onAddCard: () -> Unit
) {
    ElevatedCard(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(28.dp),
        colors = CardDefaults.elevatedCardColors(containerColor = Color.White.copy(alpha = 0.92f))
    ) {
        Column(
            modifier = Modifier.padding(18.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            SectionHeader(title = "Quản lý RFID", icon = Icons.Rounded.CreditCard)

            Text(
                text = "Nhập UID RFID đúng định dạng mà ESP32 trả về. Ví dụ: DE AD BE EF",
                style = MaterialTheme.typography.bodyMedium,
                color = DeepSea.copy(alpha = 0.75f)
            )

            OutlinedTextField(
                value = uiState.rfidInput,
                onValueChange = onRfidInputChanged,
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
                shape = RoundedCornerShape(18.dp),
                label = { Text("UID RFID") },
                leadingIcon = {
                    Icon(Icons.Rounded.CreditCard, contentDescription = null)
                }
            )

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Button(
                    onClick = onAddCard,
                    modifier = Modifier.weight(1f),
                    shape = RoundedCornerShape(18.dp)
                ) {
                    Text("Thêm thẻ")
                }

                OutlinedButton(
                    onClick = {},
                    modifier = Modifier.weight(1f),
                    enabled = false,
                    shape = RoundedCornerShape(18.dp)
                ) {
                    Text("Đã lưu ${uiState.cards.size}")
                }
            }

            if (uiState.device.lastRfidUid.isNotBlank()) {
                HorizontalDivider()
                Text(
                    text = "Thẻ quét gần nhất: ${uiState.device.lastRfidUid}",
                    style = MaterialTheme.typography.bodyMedium,
                    fontWeight = FontWeight.SemiBold
                )
                Text(
                    text = if (uiState.device.lastAccessGranted) {
                        "Kết quả gần nhất: hợp lệ"
                    } else {
                        "Kết quả gần nhất: bị từ chối"
                    },
                    style = MaterialTheme.typography.bodyMedium,
                    color = if (uiState.device.lastAccessGranted) DeepSea else Clay
                )
            }
        }
    }
}

@Composable
private fun RfidCardRow(
    uid: String,
    onRemove: () -> Unit
) {
    ElevatedCard(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(22.dp),
        colors = CardDefaults.elevatedCardColors(containerColor = Color.White.copy(alpha = 0.94f))
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 14.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Row(
                modifier = Modifier.weight(1f),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Box(
                    modifier = Modifier
                        .size(42.dp)
                        .clip(CircleShape)
                        .background(MintMist.copy(alpha = 0.45f)),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(Icons.Rounded.CreditCard, contentDescription = null, tint = DeepSea)
                }
                Spacer(modifier = Modifier.width(12.dp))
                Column {
                    Text(
                        text = uid,
                        style = MaterialTheme.typography.titleMedium,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        fontWeight = FontWeight.SemiBold
                    )
                    Text(
                        text = "Lưu trên ESP32",
                        style = MaterialTheme.typography.bodySmall,
                        color = DeepSea.copy(alpha = 0.6f)
                    )
                }
            }

            IconButton(onClick = onRemove) {
                Icon(
                    imageVector = Icons.Rounded.DeleteOutline,
                    contentDescription = "Xóa thẻ RFID",
                    tint = Clay
                )
            }
        }
    }
}

@Composable
private fun EventFeedCard(events: List<String>) {
    ElevatedCard(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(28.dp),
        colors = CardDefaults.elevatedCardColors(containerColor = Color.White.copy(alpha = 0.92f))
    ) {
        Column(
            modifier = Modifier.padding(18.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            SectionHeader(title = "Nhật ký MQTT", icon = Icons.Rounded.Wifi)

            if (events.isEmpty()) {
                Text(
                    text = "Chưa có sự kiện MQTT.",
                    style = MaterialTheme.typography.bodyMedium,
                    color = DeepSea.copy(alpha = 0.72f)
                )
            } else {
                events.forEachIndexed { index, event ->
                    Text(
                        text = event,
                        style = MaterialTheme.typography.bodyMedium,
                        color = DeepSea
                    )
                    if (index < events.lastIndex) {
                        HorizontalDivider(color = AppSand.copy(alpha = 0.35f))
                    }
                }
            }
        }
    }
}

@Composable
private fun SectionHeader(
    title: String,
    icon: androidx.compose.ui.graphics.vector.ImageVector
) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(
            modifier = Modifier
                .size(42.dp)
                .clip(RoundedCornerShape(14.dp))
                .background(MintMist.copy(alpha = 0.42f)),
            contentAlignment = Alignment.Center
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = DeepSea
            )
        }
        Spacer(modifier = Modifier.width(12.dp))
        Column {
            Text(
                text = title,
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Bold,
                color = DeepSea
            )
            Text(
                text = "Bảng điều khiển ESP32 qua MQTT",
                style = MaterialTheme.typography.bodySmall,
                color = DeepSea.copy(alpha = 0.62f)
            )
        }
    }
}

private fun brokerStatusLabel(status: BrokerStatus): String {
    return when (status) {
        BrokerStatus.CONNECTED -> "Broker đã nối"
        BrokerStatus.CONNECTING -> "Broker đang nối"
        BrokerStatus.ERROR -> "Broker lỗi"
        BrokerStatus.DISCONNECTED -> "Broker ngắt"
    }
}

private fun availabilityLabel(value: String): String {
    return if (value.equals("online", ignoreCase = true)) {
        "trực tuyến"
    } else {
        "ngoại tuyến"
    }
}
