# starseaN

An Xray client for Android.

## Features

- VPN Service, TPROXY(ROOT), TUN2SOCKS(ROOT), and BPF2SOCKS(ROOT)
- VMess, VLESS, Trojan, Shadowsocks, SOCKS, HTTP, Hysteria2, WireGuard
- v2rayNG and Mihomo subscriptions
- Profile, proxy, routing, log, and resource management
- MIUIX Compose UI

## Build

```bash
git submodule update --init --recursive
./gradlew assembleDebug
```

On Windows:

```powershell
.\gradlew.bat assembleDebug
```

## License

[GPL-3.0](LICENSE)
