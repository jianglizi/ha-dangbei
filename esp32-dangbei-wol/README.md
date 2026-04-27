# esp32-dangbei-wol

基于 ESP32-C3 + ESP-IDF v5.5 的“当贝投影仪 BLE 远程开机器”固件。

## 原理

当贝投影仪待机时会扫描 BLE 广播。ESP32 收到 `POST /api/wakeup` 后，会根据已保存或请求中传入的投影仪蓝牙 MAC 生成 `mac_based` 唤醒广播。

当前默认唤醒流程分为两阶段：

1. Phase 1：广播 Service UUID `0x1812`，Manufacturer ID `0x0046`，持续 `5000 ms`
2. Phase 2：广播 Service UUID `0xB001`，Manufacturer ID `0x013B`，持续到投影仪连接或 BLE 状态机结束

`mac_based` profile 会从投影仪 `bluetooth_mac` 计算广播负载：

- Phase 1 负载为 `00 + 反序 MAC + FF FF FF FF`
- Phase 2 负载为原始顺序 MAC

固件仍保留 `custom` profile，用于调试自定义广播数据；Home Assistant 集成默认只下发 `mac_based`。

## 构建与烧录

```bash
source /opt/esp/v5.5/esp-idf/export.sh
cd esp32-dangbei-wol
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## 首次配网

1. 烧录后 ESP32 若无 WiFi 凭据或连不上，会自动开启 SoftAP：`DangbeiWOL-XXXX`（XXXX = MAC 后 4 hex，大写）。
2. 手机/电脑连接该 AP（开放认证，无密码）。
3. 浏览器访问 `http://192.168.4.1/`，页面会自动扫描附近 WiFi，也可以手动填写 SSID。
4. 填写家用 WiFi 的 SSID、密码，以及可选的 API Bearer Token 后提交。
5. ESP32 会把 SSID、密码、Token 写入 NVS 并自动重启，1 到 2 秒后以 STA 模式上线。

SoftAP 模式下启用了轻量 captive portal DNS，常见手机/电脑会自动弹出配网页面。

## HTTP REST API

STA 模式下可用。若在配网页面设置了 Token，则以下 `/api/*` 接口都需要：

```bash
Authorization: Bearer <token>
```

| 方法 | 路径 | 行为 | 返回 |
| --- | --- | --- | --- |
| GET | `/api/info` | 设备信息 + 当前唤醒配置 | `{id, fw, api_version, chip, mac, ip, uptime_s, wake_profile, bluetooth_mac}` |
| GET | `/api/status` | 运行状态 | `{ble_busy, wake_count, last_wake_s_ago, rssi, free_heap}` |
| POST | `/api/wakeup` | 触发两阶段 BLE 唤醒（非阻塞） | `202 {started:true, phase1_ms:5000}` 或 `409 {started:false, reason:"busy"}` |
| POST | `/api/wakeup_config` | 保存唤醒配置到 NVS | `200 {profile, bluetooth_mac}` |
| POST | `/api/reset_wifi` | 擦除 WiFi 凭据并重启 | `200 {ok:true}` |

### `/api/wakeup`

可以不带 body，直接使用 NVS 中已保存的配置：

```bash
curl -X POST http://<esp32-ip>/api/wakeup
```

也可以在请求中临时传入投影仪蓝牙 MAC。固件会保存该 MAC，并按 `mac_based` profile 触发唤醒：

```bash
curl -X POST http://<esp32-ip>/api/wakeup \
  -H 'Content-Type: application/json' \
  -d '{"wake_profile":"mac_based","bluetooth_mac":"AA:BB:CC:DD:EE:FF"}'
```

`/api/wakeup` 使用 `wake_profile` 字段：

```json
{
  "wake_profile": "mac_based",
  "bluetooth_mac": "AA:BB:CC:DD:EE:FF"
}
```

### `/api/wakeup_config`

Home Assistant 保存配置或选项时会调用该接口：

```bash
curl -X POST http://<esp32-ip>/api/wakeup_config \
  -H 'Content-Type: application/json' \
  -d '{"profile":"mac_based","bluetooth_mac":"AA:BB:CC:DD:EE:FF"}'
```

请求体：

```json
{
  "profile": "mac_based",
  "bluetooth_mac": "AA:BB:CC:DD:EE:FF"
}
```

`custom` profile 仍可用于调试：

```json
{
  "profile": "custom",
  "custom_format": "manufacturer_data | full_adv",
  "custom_hex": "允许空格和换行，保存时会规范化"
}
```

说明：

- `mac_based` 必须配置非零 `bluetooth_mac`，否则触发唤醒会失败。
- `custom_format = manufacturer_data` 时，`custom_hex` 期望填写完整 Manufacturer Data 负载，最大 16 字节。
- `custom_format = full_adv` 时，`custom_hex` 期望填写完整 Advertising Data，最大 31 字节。

### 常用调试命令

```bash
curl http://<esp32-ip>/api/info | jq
curl http://<esp32-ip>/api/status | jq
curl -H 'Authorization: Bearer <token>' http://<esp32-ip>/api/info | jq
```

## 配网页面接口

SoftAP 配网页面还提供：

| 方法 | 路径 | 行为 |
| --- | --- | --- |
| GET | `/` | 配网页面 |
| GET | `/scan` | 返回附近 WiFi：`{"networks":[{"ssid","rssi","auth"}]}` |
| POST | `/save` | 保存 WiFi + Token 并重启 |

## mDNS 服务

- 服务类型：`_dangbei-wol._tcp.local.`
- 端口：`80`
- Hostname：`dangbei-wol-<id>.local`，`<id>` 为 MAC 后 6 位 hex
- TXT：`id=<id>`，`fw=<version>`，`chip=esp32c3`

Home Assistant 的 `custom_components/dangbei` 通过 zeroconf 自动发现本设备，并可预填 WOL 地址。

## 分区与存储

- `nvs`（0x6000）：WiFi 凭据、Token、唤醒配置
- `factory`（1.9MB）：应用固件
- `storage`（0x10000）：预留

## 代码结构

```text
main/
├── main.c              启动顺序：NVS → 事件循环 → WiFi → 配网/STA → HTTP → BLE
├── app_wifi.*          STA/AP 切换，NVS 凭据持久化
├── app_provision.*     SoftAP captive portal
├── app_http.*          REST API
├── app_ble_wakeup.*    NimBLE 两阶段广播状态机
├── app_wake_profile.*  wake profile 校验、NVS 持久化、广播构造
├── app_mdns.*          mDNS 服务发布
└── app_status.*        运行时统计（唤醒次数、最近唤醒时间）
```

## 注意

- 当前配网 AP 为开放网络，仅用于局域网首启配网。完成后 AP 关闭。
- 不支持 5GHz WiFi，ESP32-C3 仅支持 2.4GHz。
- 若更换 WiFi：`curl -X POST http://<ip>/api/reset_wifi`；若设置了 Token，请同时带 `Authorization` 头。
- 升级到 API v3 后，建议重新保存一次 Home Assistant 集成配置，让 HA 把投影仪 `bluetooth_mac` 同步到 ESP32。
