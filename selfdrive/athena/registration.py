#!/usr/bin/env python3
import time
import json
import jwt
import random, string
import subprocess
from pathlib import Path
from typing import Optional

from datetime import datetime, timedelta
from openpilot.common.api import api_get
from openpilot.common.params import Params
from openpilot.common.spinner import Spinner
from openpilot.selfdrive.controls.lib.alertmanager import set_offroad_alert
from openpilot.system.hardware import HARDWARE, PC
from openpilot.system.hardware.hw import Paths
from openpilot.common.swaglog import cloudlog


UNREGISTERED_DONGLE_ID = "UnregisteredDevice"

def create_new_keys(spinner: Spinner):
  try:
    result = subprocess.run(
      ['sudo', 'bash', '/data/openpilot/selfdrive/athena/generate_keys.sh'],
      check=False,                 # Don't raise an exception if the script fails
      stdout=subprocess.PIPE,      # Capture standard output
      stderr=subprocess.PIPE       # Capture standard error
    )
    # Output the results
    print("Script output:", result.stdout.decode())
    print("Script error (if any):", result.stderr.decode())
    time.sleep(2)
    spinner.update(f"SSL Keys generated. Device will attempt to register now.")
  except subprocess.CalledProcessError as e:
    spinner.update("Failed to generate keys")
    Params().put("DongleId", UNREGISTERED_DONGLE_ID)
    print("Script failed with return code:", e.returncode)
    print("Error message:", e.stderr.decode())

def is_registered_device() -> bool:
  dongle = Params().get("DongleId", encoding='utf-8')
  return dongle not in (None, UNREGISTERED_DONGLE_ID)


def register(show_spinner=False) -> Optional[str]:
  params = Params()
  params.put("SubscriberInfo", HARDWARE.get_subscriber_info())

  IMEI = params.get("IMEI", encoding='utf8')
  HardwareSerial = params.get("HardwareSerial", encoding='utf8')
  dongle_id: Optional[str] = params.get("DongleId", encoding='utf8')
  needs_registration = None in (IMEI, HardwareSerial, dongle_id)
  print(f"{IMEI=} {HardwareSerial=} {dongle_id=}")
  pubkey = Path(Paths.persist_root()+"/comma/id_rsa.pub")
  if not pubkey.is_file():
    dongle_id = UNREGISTERED_DONGLE_ID
    print(f"missing public key: {pubkey}")
    cloudlog.warning(f"missing public key: {pubkey}")
    if show_spinner:
      spinner = Spinner()
      spinner.update("No SSL keys found. Creating SSL keys")
      create_new_keys(spinner)
  if needs_registration:
    if show_spinner:
      spinner = Spinner()
      spinner.update("registering device")

    # Create registration token, in the future, this key will make JWTs directly
    with open(Paths.persist_root()+"/comma/id_rsa.pub") as f1, open(Paths.persist_root()+"/comma/id_rsa") as f2:
      public_key = f1.read()
      private_key = f2.read()

    # Block until we get the imei
    serial = HARDWARE.get_serial()
    start_time = time.monotonic()
    imei1: Optional[str] = None
    imei2: Optional[str] = None
    while imei1 is None and imei2 is None:
      try:
        imei1, imei2 = HARDWARE.get_imei(0), HARDWARE.get_imei(1)
      except Exception:
        cloudlog.exception("Error getting imei, trying again...")
        time.sleep(1)

      if time.monotonic() - start_time > 60 and show_spinner:
        spinner.update(f"registering device - serial: {serial}, IMEI: ({imei1}, {imei2})")

    params.put("IMEI", imei1)
    params.put("HardwareSerial", serial)

    backoff = 0
    start_time = time.monotonic()
    while True:
      try:
        register_token = jwt.encode({'register': True, 'exp': datetime.utcnow() + timedelta(hours=1)}, private_key, algorithm='RS256')
        cloudlog.info("getting pilotauth")
        resp = api_get("v2/pilotauth/", method='POST', timeout=15,
                       imei=imei1, imei2=imei2, serial=serial, public_key=public_key, register_token=register_token)
        print(f"{resp.status_code=}")
        if resp.status_code in (402, 403):
          if resp.status_code == 403:
            spinner.update("Bad SSL keys found. Creating new SSL keys")
            create_new_keys(spinner)
            if pubkey.is_file():
              with open(Paths.persist_root()+"/comma/id_rsa.pub") as f1, open(Paths.persist_root()+"/comma/id_rsa") as f2:
                public_key = f1.read()
                private_key = f2.read()
          cloudlog.info(f"Unable to register device, got {resp.status_code}")
          dongle_id = ''.join(random.choices(string.ascii_lowercase + string.digits, k=16))
          params.put_bool("FireTheBabysitter", True)
          params.put_bool("NoLogging", True)
        else:
          dongleauth = json.loads(resp.text)
          dongle_id = dongleauth["dongle_id"]
        break
      except Exception as e:
        print(e)
        cloudlog.exception("failed to authenticate")
        backoff = min(backoff + 1, 15)
        time.sleep(backoff)

      if time.monotonic() - start_time > 60 and show_spinner:
        spinner.update(f"registering device - serial: {serial}, IMEI: ({imei1}, {imei2})")

    if show_spinner:
      spinner.close()

  if dongle_id:
    params.put("DongleId", dongle_id)
    set_offroad_alert("Offroad_UnofficialHardware", (dongle_id == UNREGISTERED_DONGLE_ID) and not PC)
  return dongle_id


if __name__ == "__main__":
  print(register())
