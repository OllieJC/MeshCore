# Hardware smoke test. Connect the SCD41 and Pimoroni MiCS-6814 on D4/D5 and power via USB.
# After one minute of sampling, run OpenOCD with board/esp32s3-builtin.cfg,
# then xtensa-esp32s3-elf-gdb -batch firmware.elf -x this-file.
target remote localhost:3333
set $size = the_mesh.telemetry._cursor
set $buf = the_mesh.telemetry._buffer

if the_mesh.num_alert_tasks != 0
  echo Unexpected alert queued on the USB-powered sensor\n
  detach
  quit 1
end

# The last environmental records precede the XIAO's four-byte MCU temperature.
if $size < 26 || $buf[$size-4] != 1 || $buf[$size-3] != 103
  echo Incomplete sensor-node telemetry\n
  detach
  quit 1
end
set $end = $buf + $size - 4
if $size >= 34 && $end[-7] == 228
  set $end = $end - 8
  printf "PMSA003I channel %u: PM1=%u PM2.5=%u PM10=%u ug/m3\n", $end[0], ($end[2] << 8) | $end[3], ($end[4] << 8) | $end[5], ($end[6] << 8) | $end[7]
end
set $p = $end - 22
set $channel = $p[0]
set $ppm = ($p[2] << 8) | $p[3]
if $p[1] != 125 || $ppm == 0
  echo Missing SCD41 concentration\n
  detach
  quit 1
end
printf "SCD41 channel %u: %u ppm\n", $channel, $ppm
set $p = $p + 4
while $p < $end
  set $channel = $channel + 1
  set $ohms = ((unsigned)$p[2] << 24) | ($p[3] << 16) | ($p[4] << 8) | $p[5]
  if $p[0] != $channel || $p[1] != 100 || $ohms == 0
    echo Missing MiCS-6814 resistance\n
    detach
    quit 1
  end
  printf "MiCS-6814 channel %u: %u ohms\n", $channel, $ohms
  set $p = $p + 6
end
detach
