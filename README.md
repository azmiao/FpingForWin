# Fping for Windows

一个基于 [schweikert/fping](https://github.com/schweikert/fping) 设计逻辑移植到 Windows 的 fping 工具

## 功能

- IPv4（`-6` 切换 IPv6）
- 多主机并发 ping（命令行、`-f` 文件、`-g` CIDR/范围生成）
- `-a` 只显示存活 / `-u` 只显示不可达
- `-c N` 发包次数统计模式、`-C N` 逐包耗时模式、`-l` 循环模式
- `-i` 发包间隔、`-t` 超时、`-r` 重试（默认 3）、`-B` 退避因子（默认 1.5）
- `-p` 单目标周期（默认 1000ms，loop/count 下驱动发包间隔与超时）
- `-s` 退出统计、`-q` 安静模式、`-Q N` 周期统计、`-D` 时间戳（`[%.5f]` 秒）
- `-S` 指定源地址、`-b` 自定义包大小
- 回显格式对齐上游：`HOST : [SEQ], N bytes, RTT ms (AVG avg, LOSS% loss)` / `HOST : [SEQ], timed out (NaN avg, 100% loss)`
- count 末行统计：`HOST : xmt/rcv/%loss = s/r/%, min/avg/max = a/b/c`（`-C` 改为逐包耗时序列）
- 统计输出走 stderr，回显走 stdout（与上游一致）
- 退出码语义与上游一致（0 全部存活 / 1 部分不可达 / 2 有未知地址）

## 本地编译

需要 MinGW-w64（gcc）：

```sh
make
make clean
```

## 使用示例

```sh
# 扫一个网段，只显示存活主机
fping -a -g 192.168.1.0/24

# 对多个目标发 4 个包并统计（末行走 stderr）
fping -c 4 8.8.8.8 1.1.1.1 baidu.com

# -C：每个包的耗时序列
fping -C 5 -q somehost

# 从文件读目标，间隔 10ms，超时 200ms
fping -f targets.txt -i 10 -t 200

# 循环 ping 并显示时间戳（[秒.5位小数]）
fping -l -D 192.168.1.1

# 每 10 秒打一次周期统计
fping -l -Q 10 192.168.1.1
```

## 许可证

上游 fping 使用 BSD-like 许可证（Stanford University），本项目沿用相同的许可证。
详见 `LICENSE`。
