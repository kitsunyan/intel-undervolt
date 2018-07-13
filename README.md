# intel-undervolt

intel-undervolt is a tool for undervolting Haswell and newer Intel CPUs using MSR.
This tool is based on the content of [this article](https://github.com/mihic/linux-intel-undervolt).

This tool also allow to alter power limits and temperature limit using MSR and MCHBAR registers.

## Disclaimer

This tool may damage your hardware since it uses reverse engineered methods of MSR usage.
Use it on your own risk.

## Building and Installing

Run `make && make install` to build and install intel-undervolt to your system.
Run `systemctl daemon-reload` to reload unit files.

## Configuration

You can configure parameters in `/etc/intel-undervolt.conf` file.

### Voltage Planes

By default it contains all voltage planes like in ThrottleStop utility for Windows.

The following syntax is used in the file: `apply ${index} ${display_name} ${undervolt_value}`.
For example, you can write `apply 2 'CPU Cache' -25.84` in order to undervolt CPU cache by 25.84 mV.

### Power Limits

`tdp ${short_term} ${long_term}` can be used in order to alter short term and long term
power limits. For example, `tdp 35 25`.

### Temperature Limit

`tjoffset ${temperature_offset}` can be used in order to alter temperature limit. This value
is subtracted from max temperature level. For example, `tjoffset -20`. If max temperature level
is set to 100, the resulting limit will be set to `100 - 20 = 80°C`. Note that offsets
higher than 15°C are allowed only on Skylake and newer.

### Applying Configuration

Run `intel-undervolt read` to read current values and `intel-undervolt apply` to apply configured
values. You can apply your configuration automatically enabling `intel-undervolt.service`.
