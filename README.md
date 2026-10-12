# fping for Windows

一个基于 [schweikert/fping](https://github.com/schweikert/fping) 设计逻辑移植到 Windows 的
快速 ping 工具，使用纯 C + Winsock / IP Helper API 实现，无需第三方依赖，产出
**单个零依赖 `fping.exe`**。

## 与上游 fping 的关系

本项目完整保留了 fping 的核心设计：

| 设计要点 | 上游实现 | Windows 实现 |
|---|---|---|
| 异步轮询主循环 | `select()` + 事件队列 | `WaitForMultipleObjects()` + 相同事件队列 |
| 多主机 round-robin 发包 | ✅ | ✅ |
| 序列号映射（seqmap） | raw ICMP seq 字段 | OS 构造报文，用请求上下文映射 |
| 事件驱动超时检测 | 预分配 event 结构 | 完全相同 |
| Socket 层 | raw socket (需 root) | `IcmpSendEcho2` (普通用户即可) |
| 时间源 | `clock_gettime` | `QueryPerformanceCounter` |

**平台相关代码被隔离在 `socket4_win.c` / `socket6_win.c`**，主逻辑 `fping.c`
的控制流与上游保持一致，方便跟随上游演进。

## 功能

- IPv4（`-6` 切换 IPv6）
- 多主机并发 ping（命令行、`-f` 文件、`-g` CIDR/范围生成）
- `-a` 只显示存活 / `-u` 只显示不可达
- `-c N` 发包次数统计模式、`-l` 循环模式
- `-i` 发包间隔、`-t` 超时、`-r` 重试、`-B` 退避因子
- `-s` 退出统计、`-q` 安静模式、`-D` 时间戳
- `-S` 指定源地址、`-b` 自定义包大小
- 退出码语义与上游一致（0 全部存活 / 1 部分不可达 / 2 有未知地址）

## 本地编译

需要 MinGW-w64（gcc）：

```sh
make            # 产出 fping.exe
make clean
```

无需任何第三方库。

## CI 自动编译打包

`.github/workflows/build.yml` 在以下时机触发：

- **push 到 main/master**：编译 → 冒烟测试 → 上传 `fping-windows-x64.zip` artifact
- **打 `v*` tag**：额外创建 GitHub Release 并附带 zip
- **手动触发**（workflow_dispatch）

## 使用示例

```sh
# 扫一个网段，只显示存活主机
fping -a -g 192.168.1.0/24

# 对多个目标发 4 个包并统计
fping -c 4 -q 8.8.8.8 1.1.1.1 baidu.com

# 从文件读目标，间隔 10ms，超时 200ms
fping -f targets.txt -i 10 -t 200

# 循环 ping 并显示时间戳
fping -l -D 192.168.1.1
```

## 许可证

上游 fping 使用 BSD-like 许可证（Stanford University），本项目沿用相同的许可证。
详见 `LICENSE`。
