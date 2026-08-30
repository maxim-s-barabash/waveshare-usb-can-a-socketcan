# Autostart on plug-in

Starts `aa55cand` automatically when the adapter is plugged in, and stops it
when unplugged.

## Install

```
make -C ../bridge
sudo cp ../bridge/aa55cand /usr/local/bin/aa55cand
sudo cp 99-aa55can.rules /etc/udev/rules.d/
sudo cp aa55cand@.service /etc/systemd/system/
sudo udevadm control --reload-rules
sudo systemctl daemon-reload
sudo modprobe vcan   # if not already loaded/built-in
```

Unplug and replug the adapter (or run `sudo udevadm trigger`) to start it.
Check with `systemctl status aa55cand@ttyUSB0.service` and `ip -details link
show vcan0`.

## Caveats

- **CH340 has no serial number.** The udev rule matches on
  idVendor/idProduct only, so it cannot tell this adapter apart from any
  other CH340-based USB-serial device. If you have more than one plugged
  in, the service starts for each of them, all pointed at the same
  `vcan0` — not useful. If that's your setup, narrow the udev rule (e.g. by
  `KERNELS=="<usb-port-path>"` for a fixed physical port) instead of
  idVendor/idProduct.
- The CAN bus speed (`0x05` = 250 kbit/s) and target interface (`vcan0`)
  are hardcoded in `aa55cand@.service` — edit the `ExecStart` line for a
  different bus speed, or point it at a real `canX` interface if one exists
  in your setup instead of a virtual one.
