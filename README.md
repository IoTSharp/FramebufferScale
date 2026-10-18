# FramebufferScale

纯 C Linux framebuffer 虚拟化与缩放工具。应用继续使用指定的虚拟 framebuffer，适配层把画面输出到自动发现或明确指定的系统分辨率。

Pure C Linux framebuffer virtualization and scaling. The application keeps a required virtual framebuffer size while the adapter renders it to an automatically detected or explicitly selected system resolution.

## 功能 / Features

- `/etc/framebufferscale.conf` 配置，命令行参数覆盖配置。
- 虚拟分辨率必须明确，例如 `1280x720`。
- 系统分辨率支持 `auto` 或 `1280x1024`。
- `fit` 等比例居中与 `stretch` 全屏输出。
- `LD_PRELOAD` 适配库、C 启动器、无设备几何测试和 framebuffer 自测。

- Configuration from `/etc/framebufferscale.conf`, overridden by CLI options.
- A virtual size is required, for example `1280x720`.
- System size can be `auto` or an explicit value such as `1280x1024`.
- `fit` preserves aspect ratio; `stretch` fills the system framebuffer.
- Pure C preload library, launcher, geometry test, and framebuffer self-test.

## 构建 / Build

```sh
make
make check
```

在需要 GNU ABI 的目标上使用目标机或交叉工具链构建；项目不要求 Python、脚本运行时或额外服务。

Use the target or cross toolchain when a GNU ABI is required. The project does not require Python, a scripting runtime, or an external service.

## 车道启动 / Lane launch

在 `Run.sh` 中让启动器位于 `emrc_main` 之前：

```sh
./framebuffer-scale \
  --virtual-size 1280x720 \
  --system-size auto \
  --mode fit \
  -- ./emrc_main
```

`--virtual-size` 是必填项；`--system-size auto` 读取 `/dev/fb0` 的实际模式。长期运行时默认 `seconds=0`，进程退出时适配层自动清理；试验可使用 `--seconds N`。

Place the launcher before `emrc_main` in `Run.sh`. `--virtual-size` is required. `--system-size auto` reads the actual `/dev/fb0` mode. The default lifetime is the application lifetime (`seconds=0`); use `--seconds N` for a bounded trial.

直接使用预加载库时，至少设置 `SR_ATS_FB_PROGRAM`、`SR_ATS_FB_VIRTUAL_WIDTH` 和 `SR_ATS_FB_VIRTUAL_HEIGHT`；其余参数可放在配置文件或环境变量中。

When using `LD_PRELOAD` directly, set at least `SR_ATS_FB_PROGRAM`, `SR_ATS_FB_VIRTUAL_WIDTH`, and `SR_ATS_FB_VIRTUAL_HEIGHT`. Other values may come from the configuration file or environment.

## 配置 / Configuration

复制 `framebufferscale.conf.example` 到 `/etc/framebufferscale.conf`，例如：

```ini
virtual_size=1280x720
system_size=auto
mode=fit
fps=15
seconds=0
```

配置文件缺少虚拟分辨率时，适配层拒绝初始化。明确指定 `system_size` 时，实际 framebuffer 分辨率必须匹配；`auto` 不做预设。

Copy `framebufferscale.conf.example` to `/etc/framebufferscale.conf`. Initialization is rejected when the virtual size is missing. An explicit `system_size` must match the actual framebuffer; `auto` discovers it at startup.

## 验证边界 / Validation boundary

`make check` 不打开真实 framebuffer。`fb_scale_test` 需要在已批准的 Linux 车道窗口运行，并应先确认备份、回滚和人工验收条件。

`make check` does not open a real framebuffer. `fb_scale_test` requires an approved Linux lane window with backup, rollback, and operator acceptance controls.

## 许可证 / License

Unlicense / 公共领域。
