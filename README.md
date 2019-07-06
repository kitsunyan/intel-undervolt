# intel-undervolt

intel-undervolt is a tool for undervolting and throttling limits alteration for Intel CPUs.

Undervolting works on Haswell and newer CPUs and based on the content of
[this article](https://github.com/mihic/linux-intel-undervolt).

## Disclaimer

This tool may damage your hardware since it uses reverse engineered methods of MSR usage.
Use it on your own risk.

## Building and Installing

Run `./configure && make && make install` to build and install intel-undervolt to your system.

You can also configure the following features:

- `--enable-systemd` — systemd support (intel-undervolt service and intel-undervolt-loop service)
- `--enable-elogind` — elogind support (intel-undervolt system-sleep script)
- `--enable-openrc` — OpenRC support (intel-undervolt-loop service)

## Configuration

You can configure parameters in `/etc/intel-undervolt.conf` file.

### Undervolting

By default it contains all voltage domains like in ThrottleStop utility for Windows.

The following syntax is used in the file: `undervolt ${index} ${display_name} ${undervolt_value}`.
For example, you can write `undervolt 2 'CPU Cache' -25.84` to undervolt CPU cache by 25.84 mV.

### Power Limits

`power package ${short_term} ${long_term}` can be used to alter short term and long term package
power limits. For example, `power package 35 25`.

You can also specify a time window for each limit in seconds. For instance,
`power package 35/5 25/60` for 5 seconds and 60 seconds respectively.

### Temperature Limit

`tjoffset ${temperature_offset}` can be used to alter temperature limit. This value is subtracted
from max temperature level. For example, `tjoffset -20`. If max temperature is equal to 100°C, the
resulting limit will be set to `100 - 20 = 80°C`. Note that offsets higher than 15°C are allowed
only on Skylake and newer.

### Energy Versus Performance Preference Switch

Energy versus performance preference is a hint for hardware-managed P-states (HWP) which is used for
performance scaling.

For instance, with `performance` hint my i7-8650U is able to run at 4.2 GHz, and overall CPU
performance appears to be higher, but the clock speed is always locked to 4.2 GHz unless multiple
cores are loaded or CPU is throttled. With `balance_performance` hint, CPU increases the clock speed
only under load but never goes higher than 3.9 GHz, but performance of integrated GPU is
significantly better at the same time.

intel-undevolt is able to change HWP hint depending on the load, which allows to achive either
better performance or better battery life. This feature is available in daemon mode only which will
be described below. The following syntax is used to configure HWP hint:
`hwphint ${mode} ${algorithm} ${load_hint} ${normal_hint}`.

For instance, if I want to get high CPU and GPU performance from AC, I need to set `performance`
hint when CPU is under load but GPU isn't (`performance` hint reduces GPU performance in my case).
Hint switching can be configured depending on power consumption of `core` and `uncore`:
`hwphint switch power:core:gt:8:and:uncore:lt:3 performance balance_performance`. `intel_rapl`
module is required to measure power consumption.

To get a better battery life, clock speed can be reduced until CPU is under continuous high load,
which will hold the lowest CPU speed most of the time. Hint switching can be configured depending on
the CPU load: `hwphint switch load:single:0.90 balance_power power`.

Multiple `hwphint switch` rules can be used, the hint will be selected depending on current hint,
which can be configured by another tool (e.g. tlp). You can use `hwphint force` rule to set the hint
independently, but only one rule can be declared in this case.

## Usage

### Applying Configuration

Run `intel-undervolt read` to read current values and `intel-undervolt apply` to apply configured
values.

You can apply your configuration automatically enabling `intel-undervolt` service. Elogind
users should pass `yes` to `enable` option in `intel-undervolt.conf`.

### Measuring the Power Consumption

`intel_rapl` module is required to measure the power consumption. Run `intel-undervolt measure` to
display power consumption in interactive mode.

### Daemon Mode

Sometimes power and temperature limits could be reset by EC, BIOS, or something else. This behavior
can be suppressed applying limits periodically. Some features like energy vesus performance
preference switch work in daemon mode only. Use `intel-undervolt daemon` to run intel-undervolt in
daemon mode, or use `intel-undervolt-loop` service. You can change the interval using
`interval ${interval_in_milliseconds}` configuration parameter.

You can specify which actions daemon should perform using `daemon` configuration parameter. You can use `once` option to ensure action will be performed only once.
