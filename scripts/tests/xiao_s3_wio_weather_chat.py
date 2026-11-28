"""Send weather commands on #m7ojc and check the flashed XIAO's channel replies.

Usage: python xiao_s3_wio_weather_chat.py COMPANION_PORT CHANNEL_INDEX SENSOR_NAME
Configure #m7ojc on the companion and allow the fitted SCD41 to sample before running.
Requires the meshcore Python package.
"""

import asyncio
import sys

from meshcore import EventType, MeshCore


async def check(port, index, name):
    channel = int(index)
    companion = await MeshCore.create_serial(port)
    if companion is None:
        raise RuntimeError("Could not connect to companion")
    try:
        info = await companion.commands.get_channel(channel)
        if info.type != EventType.CHANNEL_INFO or info.payload["channel_name"] != "#m7ojc":
            raise RuntimeError("Expected #m7ojc in the configured channel slot")

        replies = asyncio.Queue()

        async def receive(event):
            text = event.payload["text"]
            if event.payload["channel_idx"] == channel and text.startswith(name + ": "):
                await replies.put(text)

        companion.subscribe(EventType.CHANNEL_MSG_RECV, receive)
        await companion.start_auto_message_fetching()
        for command in ("wx", "Wx", "WX", "Sensors", "sensor", "SeNsOr"):
            sent = await companion.commands.send_chan_msg(channel, command)
            if sent.type != EventType.OK:
                raise RuntimeError(f"Could not send {command}")
            reply = await asyncio.wait_for(replies.get(), timeout=20)
            assert len(reply.encode("utf-8")) <= 160, reply
            assert len(reply.splitlines()) == 4, reply
            assert all(unit in reply for unit in ("°C", "%", "hPa", "ppm", "kΩ", "µg/m³")), reply
            assert "PM1/2.5/10" in reply.splitlines()[3], reply
            assert int(reply.splitlines()[1][len("CO₂ "):].split("ppm")[0]) > 0, reply
            print(reply)
    finally:
        await companion.disconnect()


if __name__ == "__main__":
    asyncio.run(check(*sys.argv[1:]))
