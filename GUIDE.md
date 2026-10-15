# 零基础读懂 osh:一个迷你 UNIX Shell

这份文档面向几乎没学过操作系统的读者。读完后你应该能回答三个问题:

1. 这个项目要做什么?
2. 代码用到了哪些操作系统知识?
3. `osh.c` 每一部分是怎么工作的?

---

## 1. 文件夹里有什么

| 文件 | 作用 |
|---|---|
| `Lab01.md` | 作业题目(英文):要求写一个带历史记录功能的 Shell |
| `osh.c` | 作业的答案,即全部源代码(150 行 C 语言) |
| `build/osh` | 用 `gcc` 把 `osh.c` 编译出来的可执行程序(ELF 二进制,不能当文本读),由编译命令生成,不纳入版本控制 |
| `README.md` | 设计说明(中文):怎么编译、设计思路 |
| `GUIDE.md` | 就是本文 |

编译并运行:

```bash
mkdir -p build
gcc -Wall -Wextra -o build/osh osh.c
./build/osh
```

---

## 2. 先理解几个操作系统概念

### 2.1 什么是 Shell

你在终端里敲 `ls`、`cat a.txt` 时,和你对话的程序就是 **Shell**(如 bash)。它做的事情很简单,是一个循环:

```
显示提示符 → 读一行输入 → 理解这行命令 → 让系统去执行 → 回到开头
```

`osh` 就是我们自己写的一个简化版 Shell,提示符是 `osh>`。

### 2.2 什么是进程(process)

**进程 = 一个正在运行的程序**。同一个程序可以同时有多个进程。每个进程有一个编号叫 **pid**(process ID)。

进程之间有**父子关系**:一个进程可以创建新进程,创建者是父进程,被创建者是子进程。你的 Shell 就是父进程,你敲的每条命令(如 `ls`)会成为它的子进程。

为什么要这样?如果 Shell 自己直接去运行 `ls`,`ls` 结束时 Shell 也跟着没了。所以 Shell 让**子进程**去跑命令,自己继续活着。

### 2.3 系统调用(system call)

程序自己不能直接创建进程、读写磁盘,这些事要请求操作系统内核帮忙。向内核提出请求的函数就叫**系统调用**。本程序用到的核心系统调用有三个(再加一个 `waitpid`):

| 函数 | 作用 | 比喻 |
|---|---|---|
| `fork()` | 复制当前进程,产生一个子进程 | 细胞分裂 |
| `execvp(程序, 参数)` | 把当前进程的内容**替换**成另一个程序 | 同一个人换了个身份去干活 |
| `waitpid(pid, ...)` | 父进程等待子进程结束 | 等员工干完活 |

### 2.4 fork + exec 这套组合拳(最重要)

这是所有 UNIX Shell 的核心套路:

```
        父进程(osh)
            │
          fork()                  ← 复制自己
         ┌──┴──┐
       父进程   子进程
        │        │
        │      execvp("ls", ...)  ← 子进程变身成 ls
        │        │
   waitpid()     │ ls 运行……结束
   (等待)  ◄─────┘
        │
   继续显示 osh>
```

`fork()` 有个神奇之处:**调用一次,返回两次**。父、子进程各从 `fork()` 之后继续往下执行,区别是返回值:

- 返回值 `< 0`:创建失败
- 返回值 `== 0`:当前是**子进程**
- 返回值 `> 0`:当前是**父进程**,这个值就是子进程的 pid

所以代码里才有 `if (pid < 0) … else if (pid == 0) … else …` 这样的三分支。

### 2.5 前台与后台

- 普通命令(前台):父进程 `waitpid` 等它结束,才回到提示符。
- 命令末尾加 `&`(后台):父进程**不等待**,立刻回到提示符,两边同时运行。例如 `sleep 10 &`。

### 2.6 僵尸进程(zombie)

子进程结束后,内核会保留它的一点"死亡记录"(退出状态),直到父进程用 `wait` 系列函数读取。在被读取前,这个已结束的进程叫**僵尸进程**,白占系统资源。

前台命令有 `waitpid` 自然会回收;**后台命令**父进程没有等,所以代码要另外处理(见 4.7 节)。

---

## 3. 程序整体结构

```
osh.c
├── 全局数据:history 数组、total 计数器
├── add_history()     把一条命令存入历史
├── get_history(n)    取第 n 条历史命令
├── print_history()   打印历史(history 命令)
├── parse()           把一行字符串拆成单词数组,识别 &
├── reap_background() 回收已结束的后台子进程
└── main()            主循环:读输入 → 处理 → fork/exec/wait
```

`main()` 每轮循环的流程:

```
回收僵尸 → 打印 osh> → 读一行
   ├─ 读到文件结尾(Ctrl+D)  → 退出
   ├─ 太长                  → 报错,重来
   ├─ 空行                  → 重来
   ├─ 以 ! 开头             → 查历史并替换成旧命令(查不到就报错,重来)
   ├─ 存入历史
   ├─ 拆成单词(parse)
   ├─ exit                  → 结束循环
   ├─ history               → 打印历史,重来
   └─ 其他                  → fork;子进程 execvp;父进程 waitpid(或后台直接返回)
```

---

## 4. 逐段讲解 `osh.c`

### 4.1 头文件与常量(第 1–13 行)

```c
#include <stdio.h>      // printf, fgets 等输入输出
#include <stdlib.h>     // strtol 等
#include <string.h>     // strcpy, strtok, strcmp 等字符串函数
#include <unistd.h>     // fork, execvp, _exit —— UNIX 系统调用
#include <errno.h>      // errno:出错编号
#include <sys/types.h>  // pid_t 类型
#include <sys/wait.h>   // waitpid

#define MAX_LINE 80     // 一条命令最多 80 个字符(题目规定)
#define HISTORY_SIZE 10 // 历史最多保存 10 条

static char history[HISTORY_SIZE][MAX_LINE + 1]; // 10 个格子,每格存一条命令
static int total = 0;   // 到目前为止一共输入过多少条命令(也是最新一条的编号)
```

要点:

- `total` 一直递增、**不会回绕**。第 1 条命令编号 1,第 35 条编号 35。
- `static` 在这里表示"只在本文件内可见",可以先忽略。
- `MAX_LINE + 1` 中的 `+1` 是给 C 字符串末尾的结束符 `'\0'` 留位置。

### 4.2 历史记录:环形缓冲区(第 15–33 行)

只存最近 10 条,新的会覆盖最老的,这种结构叫**环形缓冲区**。关键是用"取余"决定放哪个格子:

```c
strcpy(history[total % HISTORY_SIZE], cmd);   // 第 total 条放在 total%10 号格子
total++;
```

举例(`total` 从 0 开始,存入的命令编号 = `total+1`):

| 命令编号 | 存入格子 |
|---|---|
| 1 | 0 |
| 2 | 1 |
| … | … |
| 10 | 9 |
| 11 | 0(覆盖编号 1) |
| 12 | 1(覆盖编号 2) |

取第 n 条命令:

```c
static const char *get_history(int n)
{
    if (n < 1 || n > total || n <= total - HISTORY_SIZE)
        return NULL;                          // 编号不存在,或已被覆盖
    return history[(n - 1) % HISTORY_SIZE];
}
```

三个不合法条件:

- `n < 1`:编号从 1 开始
- `n > total`:还没输入过那么多条
- `n <= total - 10`:太老了,已被覆盖。例如 `total = 35`,只有 26–35 可用

打印历史是从新到旧:

```c
for (int n = total; n >= 1 && n > total - HISTORY_SIZE; n--)
    printf("%d %s\n", n, get_history(n));
```

### 4.3 解析命令:`parse`(第 35–58 行)

目标:把 `"ps -ael"` 变成数组

```
args[0] = "ps"
args[1] = "-ael"
args[2] = NULL      ← 数组必须以 NULL 结尾,execvp 靠它知道参数到哪结束
```

```c
char *tok = strtok(line, " \t\n");
while (tok != NULL && argc < MAX_LINE / 2) {
    args[argc++] = tok;
    tok = strtok(NULL, " \t\n");
}
args[argc] = NULL;
```

- `strtok` 按空格、制表符、换行切分字符串。第一次传字符串,之后传 `NULL` 表示"接着上次继续切"。
- 它是**原地切分**:直接把原字符串里的分隔符改成 `'\0'`,`args[i]` 只是指向原字符串中的各个位置。所以 `line` 必须在整个命令执行期间保持有效。
- 最多 `MAX_LINE / 2 = 40` 个单词(80 个字符最多放 40 个单字符单词),`args` 数组大小是 `MAX_LINE/2 + 1` 刚好留 1 个位置给 `NULL`。

然后检测 `&`:

```c
*background = 0;
if (argc > 0) {
    size_t len = strlen(args[argc - 1]);          // 最后一个单词的长度
    if (args[argc - 1][len - 1] == '&') {         // 它是否以 & 结尾
        *background = 1;
        if (len == 1)  args[--argc] = NULL;       // 单独的 "&":删掉这个单词
        else           args[argc - 1][len - 1] = '\0'; // "ls&":把 & 抹掉,剩 "ls"
    }
}
```

所以 `sleep 5 &` 和 `sleep 5&` 都能识别。`background` 通过**指针参数**传出去(C 里函数想"返回多个值"的常见做法)。

### 4.4 `main` 的开头(第 66–72 行)

```c
char *args[MAX_LINE / 2 + 1]; // 解析后的参数数组(题目给的)
int should_run = 1;           // 为 0 时退出主循环(题目给的)
char input[MAX_LINE + 2];     // 读入缓冲:80 字符 + '\n' + '\0'
char line[MAX_LINE + 1];      // input 的副本,交给 parse 去破坏
```

为什么要有 `line` 这个副本?因为 `parse` 会修改字符串。历史里存的必须是完整的原始命令,所以先存历史,再把副本给 `parse` 去切。

### 4.5 读取一行与长度检查(第 73–93 行)

```c
reap_background();
printf("osh>");
fflush(stdout);
```

`fflush(stdout)` 强制立刻把提示符显示出来。`printf` 默认有缓冲(攒够了或遇到换行才输出),而 `osh>` 后面没有换行,不 flush 的话你可能看不到提示符。

```c
if (fgets(input, sizeof(input), stdin) == NULL) {
    printf("\n");
    break; /* EOF */
}
```

`fgets` 读一行。返回 `NULL` 表示输入结束(在终端按 **Ctrl+D**,或输入来自已读完的文件),此时退出循环。

```c
if (strchr(input, '\n') == NULL && !feof(stdin)) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF) ;   // 把这行剩下的字符丢掉
    printf("Command too long (at most %d characters).\n", MAX_LINE);
    continue;
}
```

`fgets` 最多读 81 个字符。如果读到的内容里**没有换行符**,说明这行还没读完,即命令超长。此时要把剩下部分读掉丢弃,否则它们会被当成下一条命令。`!feof(stdin)` 用来排除"最后一行没有换行就结束了"的正常情况。

```c
input[strcspn(input, "\n")] = '\0';           // 把末尾换行符换成字符串结束符
if (input[strspn(input, " \t")] == '\0')      // 跳过开头空白后什么都没有 → 空行
    continue;
```

- `strcspn(s, "\n")`:找第一个换行符的位置。
- `strspn(s, " \t")`:数开头有多少个空格/Tab。

### 4.6 历史命令 `!!` 和 `!N`(第 95–116 行)

```c
char *p = input + strspn(input, " \t");   // p 指向第一个非空白字符
if (p[0] == '!') {
```

**`!!`**:执行最近一条。

```c
if (strcmp(p, "!!") == 0) {
    if (total == 0) { printf("No commands in history.\n"); continue; }
    cmd = get_history(total);
}
```

**`!N`**:执行第 N 条。

```c
char *end;
errno = 0;
long n = strtol(p + 1, &end, 10);
if (end == p + 1 || *end != '\0' || errno != 0 || n > total
    || (cmd = get_history((int)n)) == NULL) {
    printf("No such command in history.\n");
    continue;
}
```

`strtol` 把字符串转成整数,`end` 会指向"转换停下来的位置"。出错条件依次是:

1. `end == p + 1`:感叹号后面一个数字都没有(如 `!abc`)
2. `*end != '\0'`:数字后面还有别的东西(如 `!3x`)
3. `errno != 0`:数字太大溢出
4. `n > total`:先判断,避免把一个很大的 `long` 强转成 `int` 出问题
5. `get_history` 返回 `NULL`:编号不在可用范围

这几个条件用 `||` 连接,**短路求值**,从左到右,前面成立后面就不再算。

成功后:

```c
strcpy(input, cmd);
printf("%s\n", input);   // 回显:让用户看到到底执行了什么(题目要求)
```

之后 `input` 里已经是旧命令了,代码继续往下走,就像用户亲手输入的一样。

### 4.7 存历史、解析、内置命令(第 118–133 行)

```c
add_history(input);          // 注意:在 !! / !N 展开之后才存,所以存的是展开后的命令
strcpy(line, input);
int argc = parse(line, args, &background);
if (argc == 0) continue;
```

由于是展开之后才存,**历史里永远不会出现 `!!` 或 `!N`**,只有真实命令。出错的 `!!` / `!N` 因为在前面 `continue` 了,也不会被存。`history` 和 `exit` 本身会被存。

**内置命令**(`exit`、`history`):

```c
if (strcmp(args[0], "exit") == 0)    { should_run = 0; continue; }
if (strcmp(args[0], "history") == 0) { print_history(); continue; }
```

为什么这两个不用 fork?因为它们要修改 **Shell 自己**的状态(退出循环、读 Shell 的历史数组)。子进程有独立的内存,它改的是自己的副本,影响不到父进程。同理真正的 Shell 里 `cd` 也必须是内置命令。

### 4.8 创建子进程执行命令(第 135–147 行)

```c
fflush(stdout);
pid_t pid = fork();
```

这里先 `fflush(stdout)` 很关键:如果 `stdout` 缓冲区里还有没输出的内容,`fork` 会把缓冲区一并复制给子进程,导致同样内容被打印两次。

```c
if (pid < 0) {
    perror("fork");                 // 创建失败,打印原因
} else if (pid == 0) {              // ---- 子进程 ----
    execvp(args[0], args);          // 变身为目标程序
    perror(args[0]);                // 只有 exec 失败才会走到这里
    _exit(127);                     // 子进程自杀,127 是"找不到命令"的惯例
} else if (background) {            // ---- 父进程,后台 ----
    printf("[background process pid %d]\n", pid);
} else {                            // ---- 父进程,前台 ----
    waitpid(pid, NULL, 0);          // 阻塞等待这个子进程结束
}
```

- `execvp` 的 `v` 表示参数用数组传,`p` 表示会在环境变量 `PATH` 里自动查找程序(所以写 `ls` 而不用写 `/bin/ls`)。
- **`execvp` 成功就永远不会返回**,因为这个进程已经被替换成别的程序了。所以它后面的代码只在出错时才会运行。
- 必须用 `_exit` 而不是 `exit`:`exit` 会刷新缓冲区并运行清理函数,而这些缓冲区是从父进程复制来的,会导致重复输出等问题;`_exit` 直接结束,不做这些事。
- `waitpid(pid, NULL, 0)` 指定只等**这一个**孩子;若期间某个后台进程结束了,不会被误当成前台命令结束。

### 4.9 回收后台进程(第 60–64 行)

```c
static void reap_background(void)
{
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}
```

- `-1`:等待**任意**子进程。
- `WNOHANG`:**不阻塞**。有已结束的子进程就回收并返回其 pid(>0),没有就立刻返回 0。
- 循环直到没有可回收的为止。

它在每次显示提示符前调用一次,把已经结束的后台进程清理掉,避免僵尸进程。

---

## 5. 动手试一试

```
osh> ls
osh> ls -l
osh> sleep 5 &
[background process pid 12345]    ← 立刻回到提示符,5 秒后该进程在后台结束
osh> history
4 history        ← history 命令自己也被记下了,而且是最新的一条
3 sleep 5 &
2 ls -l
1 ls
osh> !!          ← 重新执行最近一条(即 history),并回显
osh> !2          ← 重新执行第 2 条 ls -l
osh> !99
No such command in history.
osh> exit
```

建议再试几件事帮助理解:

- `ps` 或 `ps -ef`:在 `sleep 30 &` 之后看看有没有 `osh` 的子进程,再等它结束后敲一下回车,看它是否消失。
- 输入一个不存在的命令,如 `abc`:会看到 `abc: No such file or directory`,说明 `execvp` 失败且子进程 `_exit(127)`。
- 输入一行超过 80 个字符的命令,看报错。
- 在终端按 Ctrl+D,看 Shell 如何退出。

---

## 6. 已知的小局限(了解即可)

- **后台僵尸的回收有延迟**:`reap_background` 只在显示提示符前运行。后台进程结束后,在你下一次按回车之前,它仍是僵尸。
- **不支持引号、管道、重定向**:`echo "a b"` 会被拆成 `"a` 和 `b"`;`ls | wc`、`ls > a.txt` 无效。这些超出本作业范围。
- **没有处理 `cd`**:`cd` 需要内置实现,本作业没要求,所以 `cd` 会报找不到命令。
- `README.md` 是中文,而源码里的提示信息已经是英文。

---

## 7. 术语速查

| 术语 | 含义 |
|---|---|
| 进程 | 正在运行的程序 |
| pid | 进程编号 |
| 父/子进程 | 创建者/被创建者 |
| 系统调用 | 向操作系统内核请求服务的函数 |
| `fork` | 复制当前进程 |
| `exec` | 用另一个程序替换当前进程 |
| `wait` / `waitpid` | 父进程等待并回收子进程 |
| 前台 / 后台 | 父进程等 / 不等子进程 |
| 僵尸进程 | 已结束但父进程还没回收的进程 |
| 环形缓冲区 | 固定大小、满了就覆盖最旧数据的存储结构 |
| 内置命令 | 由 Shell 自己执行、不创建子进程的命令 |
| EOF | 输入结束(终端里按 Ctrl+D) |
