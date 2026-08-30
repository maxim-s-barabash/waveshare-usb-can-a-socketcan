# Waveshare USB-CAN serial protocol (official spec)

Source: https://www.waveshare.com/wiki/Secondary_Development_Serial_Conversion_Definition_of_CAN_Protocol

UART default: 2 Mbps, 8N1 (8 data bits, 1 stop bit, no parity).
Two selectable framing modes (set via the config command below): variable-length, or fixed 20-byte. They are not interoperable — pick one.

## Fixed 20-byte data frame (0x01)

| Byte | Field              | Notes |
|------|--------------------|-------|
| 0    | 0xAA               | header |
| 1    | 0x55               | header |
| 2    | 0x01               | type: data frame |
| 3    | frame type         | 0x01 standard, 0x02 extended |
| 4    | frame format       | 0x01 data frame (0x02 = remote, by analogy with config cmd) |
| 5-8  | CAN ID, little-endian (byte5=bits0-7 ... byte8=bits24-31) | e.g. ID 0x123 -> 23 01 00 00 |
| 9    | DLC                | 0-8 |
| 10-17| data bytes         | pad unused with 0x00 |
| 18   | reserved           | 0x00 |
| 19   | checksum           | low 8 bits of sum(byte[2..18]) |

## Variable-length data frame

`0xAA <type> <ID bytes> <data bytes> 0x55`

- `type` byte = `0xC0 | (ext<<5) | (rtr<<4) | dlc`: bit5 frame type (0 std/1 ext), bit4 format (0 data/1 remote), bits0-3 = DLC (0-8).
- ID bytes: 2 bytes for standard (11-bit), 4 bytes for extended (29-bit) — little-endian, same convention as fixed format (e.g. ID 0x1234567 -> 67 45 23 01).
- No checksum; frame is delimited by trailing 0x55.

## CAN config command (20 bytes, sent host->device)

| Byte | Field          | Notes |
|------|----------------|-------|
| 0    | 0xAA           | |
| 1    | 0x55           | |
| 2    | type           | 0x02 = configure for fixed-20-byte mode, 0x12 = configure for variable-length mode |
| 3    | CAN baud rate  | 0x01=1M 0x02=800k 0x03=500k 0x04=400k 0x05=250k 0x06=200k 0x07=125k 0x08=100k 0x09=50k 0x0a=20k 0x0b=10k 0x0c=5k |
| 4    | frame type     | 0x01 standard, 0x02 extended (default for outgoing frames in variable-length mode) |
| 5-8  | Filter ID      | big-endian, high byte first |
| 9-12 | Mask ID        | big-endian, high byte first |
| 13   | CAN mode       | 0x00 normal, 0x01 silent, 0x02 loopback, 0x03 loopback+silent |
| 14   | auto-retransmit| 0x00 enabled, 0x01 disabled |
| 15-18| reserved       | 0x00 |
| 19   | checksum       | low 8 bits of sum(byte[2..18]) |

Note the byte-order mismatch: data-frame CAN IDs are little-endian, but the config command's filter/mask IDs are documented big-endian.

Matches Kosmonova's `usb-can-firmware-stm32` implementation (`configureCanBus()` / `uartToCanDataFormatFix20B()` in Core/Src/main.c) byte-for-byte for the fixed-20-byte path, confirming this is a shared reference protocol across this OEM adapter family, not device-specific.
