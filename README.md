# ha-dangbei

Home Assistant 自定义集成，通过局域网 WebSocket 直连当贝投影仪，在 HA 中提供可视化按钮实体和 `remote` 实体。

仓库地址：<https://github.com/jianglizi/ha-dangbei>

## 功能

集成会创建一个 `Dangbei Projector` 设备，包含：

- 11 个 `button` 实体：`up`、`down`、`left`、`right`、`ok`、`back`、`home`、`menu`、`volume_up`、`volume_down`、`power_off`
- 1 个 `remote` 实体：支持 `remote.send_command` 和 `remote.turn_off`

发送 `power_off` 后，集成会按默认 2 秒延迟自动补发一次 `ok`，用于关闭关机确认弹窗。这个行为可在选项中关闭或调整延迟。

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
    ├── brand/
    ├── button.py
    ├── client.py
    ├── config_flow.py
    ├── const.py
    ├── manifest.json
    ├── remote.py
    └── translations/
```

然后重启 Home Assistant。

## 配置

在 Home Assistant 中依次进入：

`设置 -> 设备与服务 -> 添加集成 -> Dangbei Projector`

主要配置项：

| 字段 | 说明 |
| --- | --- |
| Host / IP | 投影仪局域网 IP，例如 `192.168.8.195` |
| Port | WebSocket 端口，默认 `6689` |
| Device ID | 抓包中 `data.toDeviceId` 的值 |
| toId / fromId / Message type | 协议字段，大多数情况下可直接使用默认值 |
| Auto-confirm power-off dialog | 发送关机命令后是否自动补发一次 `ok` |
| Power-off confirm delay | 自动确认延迟，默认 2 秒 |

## 使用

### Lovelace 按钮

每个按键都会暴露为独立的 `button.*` 实体，可以直接加到卡片中。

### 自动化发送按键

```yaml
service: remote.send_command
target:
  entity_id: remote.dangbei_192_168_8_195_remote
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
  entity_id: remote.dangbei_192_168_8_195_remote
data:
  command: home
```

### 关机

```yaml
service: remote.turn_off
target:
  entity_id: remote.dangbei_192_168_8_195_remote
```

## 说明

### 开机限制

投影仪关机后 WebSocket 不在线，因此本集成无法通过网络开机。常见替代方式：

- 红外遥控
- 蓝牙按键
- 外部插座断电重启

### 抓包替换协议字段

如果默认 `toId` / `fromId` 无法控制设备，可以抓取官方 App 的 WebSocket 数据：

1. 用抓包工具监听投影仪 `6689` 端口流量。
2. 使用当贝官方遥控 App 连接投影仪并按任意按键。
3. 找到 `data.command.command == "Operation"` 的消息。
4. 记录其中的 `fromId`、`toId` 和 `data.toDeviceId`。
5. 把这些值填回集成配置。

建议抓包后退出官方 App，避免多个客户端同时占用同一会话身份。

### 协议示例

```json
{
  "sn": "",
  "data": {
    "command": {
      "value": "<1~11>",
      "params": "",
      "command": "Operation",
      "from": 901
    },
    "toDeviceId": "<configured device id>"
  },
  "toId": "<configured>",
  "fromId": "<configured>",
  "type": "HB7FxtN64oc="
}
```

## 开发与贡献

- 问题反馈：请通过 GitHub Issues 提交
- 贡献说明：见 [CONTRIBUTING.md](./CONTRIBUTING.md)
- 基础校验：仓库内置 `hassfest`、HACS 校验和轻量静态检查 workflow

## 已测试机型

- 当贝 D5X Pro（`DBD5XPRO`）

其它当贝机型理论上也可尝试，欢迎提交兼容性反馈。

## 许可证

MIT © 2026 jianglizi
