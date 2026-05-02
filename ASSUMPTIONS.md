# MCP45HVX1 Implementation Assumptions

- The default I2C address range is `0x3C-0x3F`, derived from DS20005304B Rev B
  Table 6-2 and the text-extracted official PDF. The extracted markdown notes a
  conflicting `0x5C-0x5F` range; the driver accepts both ranges, but defaults to
  `0x3C`.
- The device is treated as volatile-only. No user-accessible non-volatile memory
  API is exposed because the register map documents only Wiper 0 and TCON0 RAM.
- TCON reserved bits `[7:4]` are always written as `1`.
- The undocumented `GCEN` bit is not exposed. General Call command helpers only
  send the documented broadcast frames.
- Address `0x01` is treated as reserved despite a continuous-write figure note
  mentioning it.

