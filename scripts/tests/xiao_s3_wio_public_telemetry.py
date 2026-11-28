"""Run with a nearby USB companion that has never logged in to the sensor.

Usage: python xiao_s3_wio_public_telemetry.py COMPANION_PORT SENSOR_KEY_PREFIX
Requires the meshcore Python package and the sensor in the companion's contacts.
"""

import asyncio
import sys

from meshcore import EventType, MeshCore


async def check(port, key_prefix):
    companion = await MeshCore.create_serial(port)
    if companion is None:
        raise RuntimeError("Could not connect to companion")
    try:
        await companion.ensure_contacts()
        sensor = companion.get_contact_by_key_prefix(key_prefix)
        if sensor is None:
            raise RuntimeError("Sensor missing from companion contacts")
        advert = await companion.commands.send_advert()
        if advert.type != EventType.OK:
            raise RuntimeError("Companion could not advertise")
        for _ in range(3):
            await asyncio.sleep(5)
            readings = await companion.commands.req_telemetry_sync(sensor, timeout=10)
            if readings and {2, 3, 4, 5, 6}.issubset(r["channel"] for r in readings):
                break
        else:
            raise AssertionError("Missing public sensor readings")
        if await companion.commands.req_acl_sync(sensor, timeout=10) is not None:
            raise AssertionError("Public reader received the admin access list")
    finally:
        await companion.disconnect()


if __name__ == "__main__":
    asyncio.run(check(*sys.argv[1:]))
