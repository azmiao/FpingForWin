# 更新日志

## 1.0.0 (2026-10-13)

### ✨ 新功能

- CI 增加 changelog 生成与 Release 发布流程——tag 触发时先生成 changelog（release-notes.md + 更新 CHANGELOG.md）再创建带说明的 Release；build 仅编译不再直接建 Release @azmiao
- CI 支持 x64 + arm64 双架构编译（llvm-mingw matrix）；Makefile 支持 TRIPLE 指定目标三元组 @azmiao
- fping Windows 移植版（IcmpSendEcho2 异步模型 + Actions 自动编译） @azmiao

### 🐛 Bug 修复

- workflow 增加 contents:write 权限——修复 changelog job 推送 CHANGELOG.md 时 GITHUB_TOKEN 403 Permission denied @azmiao
- -a扫描卡死——opt_loop默认值应为0(默认单发+retry)，-l才置1；之前默认1导致发包后无限调度 @azmiao
- loop模式第一包不打印；loop/count per-recv输出带延迟 @azmiao
- -a 重复打印——仅首次存活打印且成功不retry；默认按地址显示(-n才反向DNS)；打印后fflush @azmiao
- -a/-u 扫描模式被 opt_quiet 抑制无输出；-a 存活只打印主机名；-e 才显示延迟 @azmiao
- 收不到回包——事件改 manual-reset 避免被 WaitForMultipleObjects 消费；事件类型用显式字段；事件存储索引取模；系统超时改为2倍fping超时；loop/count在发包后调度下一包；zip去除dist目录层级 @azmiao
- count 模式 timeout 后未调度下一包（导致 -c 只发 1 包） @azmiao
- Package 步骤改用 PowerShell Compress-Archive（Git Bash 无 zip） @azmiao
- optparse_argtype 遍历跳过冒号，无参数选项正确返回 0 @azmiao
- 精简 icmp_win.h，移除与 Winsock 冲突的宏（Windows 由系统构造报文） @azmiao
- icmp_win.h 宏名冲突改为 FPING_ 前缀；-S 改用 IcmpSendEcho2Ex @azmiao
- 修复编译错误（time.h 引入、socket_set_src_addr_ipv4 声明） @azmiao

### 🎨 代码重构

- changelog job 改为 checkout 主分支（含全部tag），避免detached HEAD推送CHANGELOG可能的fast-forward问题 @azmiao

### 📚 文档

- 更新简化说明文档 @azmiao

### 🧹 日常维护

- 添加 FPING_DEBUG 调试输出定位 -a 扫描卡死问题 @azmiao

### 其他变更

- Initial commit @azmiao
