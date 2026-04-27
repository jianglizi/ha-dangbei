# ha-dangbei

Home Assistant 自定义集成，通过局域网 WebSocket 直连当贝投影仪，在 HA 中提供遥控按钮、`remote` 电源实体、在线状态，以及可选的 ESP32 BLE 远程开机能力。

仓库地址：<https://github.com/jianglizi/ha-dangbei>

## 功能

集成会创建一个 `Dangbei Projector` 设备，包含：

- 13 个 `button` 实体：`up`、`down`、`left`、`right`、`ok`、`back`、`home`、`menu`、`volume_up`、`volume_down`、`side_menu`、`find_remote`、`screenshot`
- 1 个 `remote` 实体：名称显示为 `Power`，支持 `remote.send_command`、`remote.turn_on`、`remote.turn_off`
- 1 个 `binary_sensor` 实体：`online`，用于反映投影仪 WebSocket 是否在线
- 配置流程会尝试通过 mDNS 发现投影仪，并通过 112 WebSocket 握手读取 `device_id`、蓝牙 MAC、设备名称、型号、ROM 等信息

如果额外配置了配套的 ESP32 开机器 [`esp32-dangbei-wol`](./esp32-dangbei-wol)，还会新增：

- 1 个独立 ESP32 设备
- 1 个 `binary_sensor.esp32_online`，用于反映 ESP32 HTTP API 是否在线
- `remote.turn_on` 会通过 ESP32 触发 BLE 广播唤醒投影仪
- 保存配置或选项时，HA 会把投影仪的 `bluetooth_mac` 下发到 ESP32，供固件生成 `mac_based` 唤醒广播

发送 `power_off` 后，集成会按默认 2 秒延迟自动补发一次 `ok`，用于关闭关机确认弹窗。这个行为可在选项中关闭或调整延迟。

点击 `Power` 开关后，集成会先进入一个快速确认窗口，再回落到基线慢轮询：

- 基线轮询默认 10 秒
- 开机后会立即刷新，并以 1 秒频率确认最多 30 秒
- 关机后会立即刷新，并以 1 秒频率确认最多 45 秒
- 确认窗口内 `Power` 会锁定目标状态，避免 UI 先反弹再收敛

## 示例

<p align="center">
  <img src="./doc/image/image.png" alt="Dangbei Projector 集成示例" width="900" />
</p>

<p align="center"><em>Home Assistant 中的 Dangbei Projector 实体示例</em></p>

## 安装

### HACS

1. 打开 HACS。
2. 进入 `Integrations`。
3. 添加自定义仓库 `https://github.com/jianglizi/ha-dangbei`，类别选择 `Integration`。
4. 搜索 `Dangbei Projector` 并安装。
5. 重启 Home Assistant。

### 手动安装

把仓库中的 [`custom_components/dangbei`](./custom_components/dangbei) 整个目录复制到 Home Assistant 的 `config/custom_components/` 下：

```text
config/custom_components/
└── dangbei/
    ├── __init__.py
    ├── binary_sensor.py
    ├── brand/
    ├── button.py
    ├── client.py
    ├── coordinators.py
    ├── config_flow.py
    ├── const.py
    ├── device_info.py
    ├── manifest.json
    ├── remote.py
    └── translations/
```

然后重启 Home Assistant。

## 配置

在 Home Assistant 中依次进入：

`设置 -> 设备与服务 -> 添加集成 -> Dangbei Projector`

配置流程会优先扫描局域网内的投影仪：

- 投影仪 mDNS 类型：`_db_controller._tcp.local.`
- 如果发现投影仪，可以直接从列表选择
- 如果没有发现，也可以选择手动输入 IP
- 投影仪需要处于开机状态，HA 才能通过 WebSocket 端口 `6689` 执行 112 握手

初始配置项：

| 字段 | 说明 |
| --- | --- |
| Discovered projector | 自动发现的投影仪；没有发现时选择手动输入 |
| Host / IP address | 手动输入的投影仪局域网 IP，例如 `192.168.8.195` |
| WebSocket port | 投影仪 WebSocket 端口，默认 `6689` |
| ESP32 WOL device host / IP | 可选；填写后启用 ESP32 远程开机 |
| ESP32 WOL device port | ESP32 HTTP 端口，默认 `80` |
| ESP32 WOL auth token | 可选；对应 ESP32 配网页面保存的 Bearer Token |
| Auto-confirm power-off dialog | 发送关机命令后是否自动补发一次 `ok` |
| Power-off confirm delay | 自动确认延迟，默认 2 秒 |

选项页还提供：

- `Online status poll interval`：投影仪和 ESP32 状态的基线轮询周期，默认 10 秒，可调 5 到 60 秒
- ESP32 WOL 地址、端口和 Token
- 高级覆盖字段：`device_id`、`toId`、`fromId`、`Message type`

说明：

- 新版本不再要求手动填写 `device_id`。配置流程会通过 112 握手自动读取。
- 如果配置了 ESP32，保存时会尝试调用 `POST /api/wakeup_config`，把 `profile=mac_based` 和投影仪 `bluetooth_mac` 同步到 ESP32。
- 如果 ESP32 通过 mDNS 发布 `_dangbei-wol._tcp.local.`，HA 可自动发现并预填 WOL 地址。
- 如果投影仪 mDNS 信息不完整，仍可通过手动 IP 继续配置，但投影仪必须开机以完成 112 握手。

## 使用

### Lovelace 按钮

方向键、菜单键、音量键、侧边栏、找回遥控器和截图都会暴露为独立的 `button.*` 实体，可以直接加到卡片中。

### 自动化发送按键

```yaml
service: remote.send_command
target:
  entity_id: remote.<your_projector_remote>
data:
  command:
    - home
    - down
    - ok
  delay_secs: 0.3
```

也支持直接传入单个命令字符串：

```yaml
service: remote.send_command
target:
  entity_id: remote.<your_projector_remote>
data:
  command: home
```

可用命令：

```text
up, down, left, right, ok, back, home, menu,
volume_up, volume_down, power_off,
side_menu, find_remote, screenshot
```

### 关机

```yaml
service: remote.turn_off
target:
  entity_id: remote.<your_projector_remote>
```

### 开机

开机需要配置 ESP32 WOL 设备：

```yaml
service: remote.turn_on
target:
  entity_id: remote.<your_projector_remote>
```

## 说明

### 开机限制

投影仪关机后 WebSocket 不在线，因此仅靠本集成本身无法通过网络开机。

如果需要远程开机，推荐配套使用 [`esp32-dangbei-wol`](./esp32-dangbei-wol)：

- ESP32-C3 固件会在局域网内提供 HTTP REST API
- HA 通过 `remote.turn_on` 调用 `POST /api/wakeup`
- HA 会把投影仪 `bluetooth_mac` 发送给 ESP32
- ESP32 使用 `mac_based` profile 生成两阶段 BLE 广播
- ESP32 会通过 mDNS 发布 `_dangbei-wol._tcp.local.`

固件烧录、配网和 API 说明见 [`esp32-dangbei-wol/README.md`](./esp32-dangbei-wol/README.md)。

### mDNS 与发现

当前涉及两个 mDNS 服务类型：

| 服务类型 | 发布方 | 用途 |
| --- | --- | --- |
| `_db_controller._tcp.local.` | 当贝投影仪 | HA 用于发现投影仪，并读取 mDNS TXT 中的基础设备信息 |
| `_dangbei-wol._tcp.local.` | ESP32 WOL 固件 | HA 用于发现 ESP32 开机器，并预填 WOL 地址 |

投影仪发现后，HA 会继续执行 112 WebSocket 握手。112 返回的数据会覆盖或补充 mDNS 信息，尤其是 `bluetooth_mac`。

### 抓包替换协议字段

如果默认 `toId` / `fromId` 无法控制设备，可以抓取官方 App 的 WebSocket 数据：

1. 用抓包工具监听投影仪 `6689` 端口流量。
2. 使用当贝官方遥控 App 连接投影仪并按任意按键。
3. 找到 `data.command.command == "Operation"` 的消息。
4. 记录其中的 `fromId`、`toId` 和 `data.toDeviceId`。
5. 到集成选项页填写高级覆盖字段。

建议抓包后退出官方 App，避免多个客户端同时占用同一会话身份。

### 协议示例

普通遥控命令：

```json
{
  "sn": "",
  "data": {
    "command": {
      "value": "<1~12, 101, 111>",
      "params": "",
      "command": "Operation",
      "from": 900
    },
    "toDeviceId": "<device_id>"
  },
  "toId": "<configured>",
  "fromId": "<configured>",
  "type": "<configured>"
}
```

112 设备信息握手：

```json
{
  "sn": "",
  "data": {
    "command": {
      "value": "112",
      "params": "",
      "command": "Tool",
      "from": 900
    },
    "toDeviceId": ""
  },
  "toId": "",
  "fromId": "",
  "type": ""
}
```

## 开发与贡献

- 问题反馈：请通过 GitHub Issues 提交
- 贡献说明：见 [CONTRIBUTING.md](./CONTRIBUTING.md)
- 基础校验：仓库内置 `hassfest`、HACS 校验和轻量静态检查 workflow
- 本地 HA 语法检查：`python3 -m compileall custom_components/dangbei`
- 本地 ESP32 固件构建：`source /opt/esp/v5.5/esp-idf/export.sh && cd esp32-dangbei-wol && idf.py build`

## 已测试机型

- 当贝 D5X Pro（`DBD5XPRO`）

其它当贝机型理论上也可尝试，欢迎提交兼容性反馈。

## 许可证

MIT © 2026 jianglizi
