# pnx-intramural-robot
# Github协作说明

PNX 校内赛机器人整机软件：视觉（MaixCam）+ 电控（C 板）

## 目录

- `vision/`：视觉——感知、跟踪、状态机、跟随投放...
- `control/`：电控——电机、底盘、机构、RFID、遥控...
- `protocol/`：视觉与电控的接口规约（版本化）
- `docs/`：整机级文档

## 协作

主仓库由管理员维护，协作者用 fork + PR 参与。所有 PR 由管理员审核后合入 `main`。

### 0. 首次认证（如果第一次用 GitHub）

GitHub 不认密码登录，HTTPS 推拉要用个人访问令牌（token）：

GitHub 网页 → 右上角头像 → Settings → Developer settings → Personal access tokens → Tokens (classic) → Generate new token，勾上 `repo`，生成后复制下来（只显示一次）。

clone / push 弹用户名密码时，用户名填 GitHub 用户名，密码填这个 token（不是登录密码）。

让电脑记住它，只输一次：

    git config --global credential.helper store

### 1. 加入与 fork

管理员在 Settings → Collaborators 里添加协作者，接受邀请后即可访问本仓库。

点仓库右上角 Fork，到自己账号下，再克隆自己的 fork：

    git clone https://github.com/<你的用户名>/pnx-intramural-robot.git

加 upstream 指回主仓库：

    git remote add upstream https://github.com/shenghaotu81-droid/pnx-intramural-robot.git

### 2. 开工前同步

    git checkout main
    git fetch upstream
    git merge upstream/main
    git push origin main

### 3. 开分支干活

一块活一个分支：

    git checkout -b feat/xxx

写代码，提交（格式 `[tag] + 一句话`）：

    git add <文件>
    git commit -m "[control] 一句话说明"
    git push -u origin feat/xxx

### 4. 提 PR

GitHub 上打开自己的 fork，点 Compare & pull request，base 选主仓库 `main`，compare 选自己的分支（刚刚开发完成的那个）。写清改了什么、为什么、怎么测的，等待管理员审核。

### 5. 审查与合并

管理员审核，有问题在 PR 里提，如果修改了继续往同一分支 add -> commit -> push 即可，PR 会自动更新。通过后管理员合并（Squash and merge）。

### 6. 合并后同步（等待审核完毕后，继续在本地开发）

重复第 2 步同步本地 `main`。分支留着做记录，不用删。

下一块活从更新后的 `main` 重新开分支，接着开发新模块。

### 7. 遇到冲突怎么办（一般不会）

分两种情况，前半段不一样，后半段一样：

**情况一：PR 页面提示冲突（本地还没合过）**

PR 显示合不了，意思是你的分支跟现在的 `main` 合不到一起。先在本地跑这三条，把冲突引出来：

    git checkout feat/xxx
    git fetch upstream
    git merge upstream/main

这时本地会报 CONFLICT，文件里出现标记，接着往下走。

**情况二：本地合并时已经报 CONFLICT**

合并正在进行中，文件里已经有标记了，不用跑上面三条，直接往下走。

**接下来两种情况一样：**

打开报冲突的文件，找到 `<<<<<<<`、`=======`、`>>>>>>>` 三组标记，留想要的内容、删掉标记，保存。

选完发现不对、还没提交就想重来，跑 `git merge --abort`，撤销这次 `merge`，回到 `git merge` 之前的状态（`fetch` 到的上游记录还在，不用重拉）。

没问题就提交推送：

    git add <冲突文件>
    git commit -m "[control] 解决与 main 的冲突"
    git push origin <当前分支>

`<当前分支>` 填你正在合的分支名：在 `feat/xxx` 上合就填 `feat/xxx`，在 `main` 上合就填 `main`。

推完，冲突消失（有 PR 的话 PR 会自动更新）。

### 规矩

- `main` 永远可用，不直接往 `main` 推（merge）代码。
- 一块活一个分支，分支名 `feat/xxx` 或 `fix/xxx`。
- 提交格式 `[tag] + 一句话`。
- 改 `protocol/`（接口）先对齐规约，不单方面改。
- ...