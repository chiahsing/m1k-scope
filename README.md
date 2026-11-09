# M1K Scope

A desktop oscilloscope, function generator and Bode plotter for the
[Analog Devices ADALM1000](https://www.analog.com/en/resources/evaluation-hardware-and-software/evaluation-boards-kits/adalm1000.html)
(M1K), written in C++20 with Qt 6.

The M1K is a two-channel source-measure unit: each channel can source a
voltage and measure current, or source a current and measure voltage, at
100 kSa/s. M1K Scope turns it into a small bench instrument with three tools
in one window.

## Features

### Realtime mode

**Oscilloscope**

- Shows all four measurements live: voltage and current on Ch A and Ch B.
  Voltage is on the left axis, current on the right one.
- Time base from 100 µs/div to 10 ms/div.
- Voltage scales from 10 mV/div to 0.5 V/div, and current scales from
  0.1 mA/div to 40 mA/div.
- X-Y mode: plot the measurements against any one of them instead of time.
- Edge trigger on any measurement, rising or falling, with hysteresis. In
  Auto mode the plot free-runs when nothing triggers; in Normal mode it keeps
  the last triggered frame.
- Mouse control:
  - Scroll over an axis to zoom it.
  - Scroll over the plot to zoom all axes around the cursor.
  - Drag the plot to pan.
  - A cursor readout shows the time and the value of every trace under the
    mouse.

**Function generator**

- Per channel: source voltage (0 to 5 V) or current (−200 to 200 mA).
- Waveforms: constant (DC), sine, square with adjustable duty cycle,
  triangle, sawtooth and stairstep.
- Periodic waveforms are set by their minimum and maximum levels, frequency
  (0.1 Hz to 10 kHz) and phase.
- Changes take effect when you click **Apply**; **Revert** goes back to the
  last applied settings.

### Bode mode

- Measures the frequency response of a circuit:
  - Ch A drives the circuit's input with a sine wave and measures it.
  - Ch B, left floating (Hi-Z), measures the output.
- Sweeps from 1 Hz up to 20 kHz, with 1 to 50 points per decade spread on a
  log scale.
- Plots gain (dB) and phase (degrees) as each point is measured.
- Corrects for the small delay between the M1K's sampling of Ch A and Ch B.
- Waits for each new frequency to reach the output and for the circuit to
  settle before measuring.

## Requirements

- Linux. Development and testing are done on Fedora; other distributions
  should work.
- An ADALM1000 connected over USB.
- [libsmu](https://github.com/analogdevicesinc/libsmu) 1.0 or later, the
  Analog Devices library for the M1K, with its udev rules installed so the
  device can be opened without root.
- Qt 6 (Core, Gui, Widgets).
- libusb 1.0.
- CMake 3.16 or later, and a C++20 compiler (GCC 11+ or Clang 14+).

On Fedora, the dependencies other than libsmu are:

```sh
sudo dnf install cmake gcc-c++ qt6-qtbase-devel libusb1-devel
```

On Debian or Ubuntu:

```sh
sudo apt install cmake g++ qt6-base-dev libusb-1.0-0-dev
```

libsmu is usually built from source; follow the instructions in its
repository. Make sure its udev rules (`53-adi-m1k-usb.rules`) end up in
`/etc/udev/rules.d/` and reload them, or the M1K will only open as root.

## Building

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Running

Connect the M1K, then:

```sh
./build/m1k_scope
```

The program exits with a message if no M1K is found. The window remembers
its size between sessions; Ctrl+W closes it.

## Source layout

| Directory | Contents |
|---|---|
| `core/` | Device access and signal processing, independent of the UI: acquisition on a background thread, triggering, waveform generation and the Bode sweep. |
| `ui/` | The Qt widgets: the main window, the plots and the control panels. |
| `resources/` | The application icon. |

## License

Copyright (C) 2026 Chiahsing Yu

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, version 3. See [LICENSE](LICENSE) for the full text.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.
