# esp32-dangbei-wol

基于 ESP32-C3 + ESP-IDF v5.5 的“当贝投影仪 BLE 远程开机器”固件。

## 原理

当贝投影仪待机时会扫描 BLE 广播。ESP32 收到 `POST /api/wakeup` 后，会按当前已保存的 wake profile 构造广播并持续约 5 秒。

当前支持的 wake profile：

- `d5x_pro`：使用 D5X Pro 实测成功的完整广播模板，并保留序号字节递增逻辑
- `f3_air`：使用 F3 Air 的 HID/Battery 广播模板
- `custom + manufacturer_data`：使用 F3 Air 外层模板，仅替换 Manufacturer Data
- `custom + full_adv`：直接发送用户提供的完整 Advertising Data

默认 profile 是 `d5x_pro`。

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
5. ESP32 会把 SSID、密码、Token 写入 NVS 并自动重启，1~2 秒后以 STA 模式上线。

SoftAP 模式下启用了轻量 captive portal DNS，常见手机/电脑会自动弹出配网页面。

## HTTP REST API（STA 模式下可用）

| 方法 | 路径 | 行为 | 返回 |
|------|------|------|------|
| GET  | `/api/info`       | 设备信息 + 当前 wake profile | `{id, fw, api_version, chip, mac, ip, uptime_s, wake_profile, wake_custom_format, wake_custom_hex}` |
| GET  | `/api/status`     | 运行状态 | `{ble_busy, wake_count, last_wake_s_ago, rssi, free_heap}` |
| POST | `/api/wakeup`     | 触发 5s BLE 广播（非阻塞） | `202 {started:true, duration_ms:5000}` 或 `409 {started:false, reason:"busy"}` |
| POST | `/api/wakeup_config` | 保存当前 wake profile 到 NVS | `200 {profile, custom_format, custom_hex}` |
| POST | `/api/reset_wifi` | 擦除 NVS 并重启 | `200 {ok:true}` |

如果在配网页面设置了 Token，则以上 `/api/*` 接口都需要：

```bash
Authorization: Bearer <token>
```

示例：

```bash
curl -X POST http://<esp32-ip>/api/wakeup
curl -X POST http://<esp32-ip>/api/wakeup_config \
  -H 'Content-Type: application/json' \
  -d '{"profile":"d5x_pro","custom_format":"manufacturer_data","custom_hex":""}'
curl http://<esp32-ip>/api/status | jq
curl -H 'Authorization: Bearer <token>' http://<esp32-ip>/api/info | jq
```

`/api/wakeup_config` 请求体固定为：

```json
{
  "profile": "d5x_pro | f3_air | custom",
  "custom_format": "manufacturer_data | full_adv",
  "custom_hex": "允许空格和换行，保存时会规范化"
}
```

说明：

- `profile != custom` 时，`custom_format` 和 `custom_hex` 会被忽略，但仍会一并保存。
- `custom_format = manufacturer_data` 时，`custom_hex` 期望填写完整 Manufacturer Data 负载，最大 16 字节。
- `custom_format = full_adv` 时，`custom_hex` 期望填写完整 Advertising Data，最大 31 字节。

## 配网页面接口

SoftAP 配网页面还提供：

| 方法 | 路径 | 行为 |
|------|------|------|
| GET  | `/`     | 配网页面 |
| GET  | `/scan` | 返回附近 WiFi：`{"networks":[{"ssid","rssi","auth"}]}` |
| POST | `/save` | 保存 WiFi + Token 并重启 |

## mDNS 服务

- 服务类型：`_dangbei-wol._tcp.local.`
- 端口：80
- Hostname：`dangbei-wol-<id>.local`，`<id>` 为 MAC 后 6 位 hex
- TXT：`id=<id>`，`fw=<version>`，`chip=esp32c3`

Home Assistant 的 `custom_components/dangbei` 通过 zeroconf 自动发现本设备。

## 分区与存储

- `nvs`（0x6000）：WiFi 凭据
- `factory`（1.9MB）：应用固件
- `storage`（0x10000）：预留

## 代码结构

```
main/
├── main.c            启动顺序：NVS → 事件循环 → WiFi → 配网/STA → HTTP → BLE
├── app_wifi.*        STA/AP 切换，NVS 凭据持久化
├── app_provision.*   SoftAP captive portal
├── app_http.*        REST API
├── app_ble_wakeup.*  NimBLE 广播触发器
├── app_wake_profile.* wake profile 校验、NVS 持久化、广播构造
├── app_mdns.*        mDNS 服务发布
└── app_status.*      运行时统计（唤醒次数、最近唤醒时间）
```

## 注意

- 当前配网 AP 为开放网络，仅用于局域网首启配网。完成后 AP 关闭。
- 不支持 5GHz WiFi，ESP32-C3 仅 2.4GHz。
- 若更换 WiFi：`curl -X POST http://<ip>/api/reset_wifi`；若设置了 Token，请同时带 `Authorization` 头。
