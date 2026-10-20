# Lab01：带历史记录功能的 UNIX Shell

## 编译与运行（Linux / WSL）

可执行文件输出到 `build/` 目录，该目录已被 `.gitignore` 忽略。

```bash
mkdir -p build && gcc -std=c99 -O2 -Wall -fno-asm "osh.c" -o "build/osh" --static -lm && "./build/osh"
```

## 设计说明

- `main()` 在 `should_run` 为 1 时循环：打印 `osh>` 提示符，用 `fgets` 读取一行。超过 80 个字符的输入会被拒绝。输入 `exit` 会把 `should_run` 置为 0 并退出。
- 输入行被切分为以 NULL 结尾的 `args` 数组。末尾的 `&`（单独一个词或紧贴最后一个词）会被去掉，并标记为后台命令。
- 父进程调用 `fork()`，子进程调用 `execvp(args[0], args)`。前台命令由父进程对该子进程调用 `waitpid()` 等待；后台命令则立即返回提示符。每次打印提示符之前，用 `waitpid(-1, WNOHANG)` 回收已结束的后台子进程，避免产生僵尸进程。
- 历史记录是 10 个槽位的环形缓冲区。全局计数器从 1 开始给命令编号，超过 10 后继续递增，因此输入 35 条命令后，缓冲区中保存的是第 26 到 35 条。
  - `history` 按从新到旧的顺序输出，格式为 `编号 命令`。
  - `!!` 重新执行最近一条命令；`!N` 重新执行第 N 条命令。命令会先回显，再执行，并作为新的一条存入历史。
  - 历史为空时输入 `!!`，输出 `No commands in history.`；`!N` 的编号不在缓冲区内（或不是合法数字）时，输出 `No such command in history.`。这两种情况都不会写入历史。
  - 其他所有非空命令（包括 `history` 本身）都会存入历史。

## 分工

单人完成。
