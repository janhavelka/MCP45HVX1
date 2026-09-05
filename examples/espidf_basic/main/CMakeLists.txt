idf_component_register(
  SRCS "main.cpp"
  INCLUDE_DIRS "." "../../.."
  REQUIRES MCP45HVX1 esp_driver_i2c esp_driver_gpio esp_timer freertos vfs
           esp_driver_uart esp_driver_usb_serial_jtag esp_usb_cdc_rom_console
)
