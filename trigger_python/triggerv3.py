"""
    File: triggerv3.py
    Brief: UDP-triggered OPAL-RT execution with persistent console and file logging.
    This version keeps the triggerv2 startup and trigger flow, but records every
    relevant stage to a timestamped log file so model upload, pause, execution,
    termination, and any available runtime counters can be reviewed later.

    Note: This script is designed to run on a Windows machine with the OPAL-RT API.
    Date: 2026-06
    @author: erg-cmd (elias.gracia@uah.es)
"""

import os
import socket
import struct
import sys
import time
from datetime import datetime

try:
    import msvcrt
except ImportError:
    msvcrt = None

""" OP_MODEL_STATE = {
    'X_MODEL_NOT_CONNECTED': 0, #: No connected model.
    'X_MODEL_NOT_LOADABLE': 1, #: Model has not been compiled.
    'X_MODEL_COMPILING': 2, #: Model is compiling.
    'X_MODEL_LOADABLE': 3, #: Model has been compiled and is ready to load.
    'X_MODEL_LOADING': 4, #: Model is loading.
    'X_MODEL_RESETTING': 5, #: Model is resetting.
    'X_MODEL_LOADED': 6, #: Model loaded on target.
    'X_MODEL_PAUSED': 7, #: Model is loaded and paused on target.
    'X_MODEL_RUNNING': 8, #: Model is loaded and executed on the target.
    'X_MODEL_DISCONNECTED': 9 #: Model is disconnected.
} """

# --- OPAL-RT API PATH CONFIGURATION ---
RTLAB_PATH = r"C:\OPAL-RT\RT-LAB\2021.3\common\python"
if RTLAB_PATH not in sys.path:
    sys.path.append(RTLAB_PATH)

try:
    import RtlabApi
except ImportError:
    print(f"Error: Could not find RtlabApi at {RTLAB_PATH}. Check your path settings.")
    sys.exit(1)

# --- NETWORK & PROJECT PARAMETERS ---
UDP_IP = "0.0.0.0"
UDP_PORT = 5008
LAUNCH_CODE = b"LAUNCH_SIM"
PROJECT_PATH = r"C:\Archivos_INI_OPAL\ergs_test5\ergs_test5.llp"
# PROJECT_PATH = r"C:\Archivos_INI_OPAL\ergs_test6\ergs_test6.llp"
NEGOTIATION_TARGET_IP = "192.168.10.123"
NEGOTIATION_TARGET_PORT = 5008
EXPECTED_READY_IP = "192.168.10.123"
EXPECTED_READY_PORT = 5008
READY_TEXT = b"READY_"
DUMMY_TEXT = b"ByeBye"
# PRESET_VALUES = [6.0, 0.003, 30.0, 2.0, 5.0, 10.0]
PRESET_VALUES = [1.0, 0.003, 180.0, 3.0, 3.0, 9.0]
# case / step / sim_time / wind_type / controller_type / mean wind speed
NEGOTIATION_TIMEOUT_SECONDS = 60.0
RESEND_PERIOD_SECONDS = 1.0
MODEL_PAUSED_STATE = 7
MODEL_RUNNING_STATE = 8

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
LOG_DIR = os.path.join(SCRIPT_DIR, "logs")
LOG_FILE_PATH = None
LOG_FILE_HANDLE = None


def log(message=""):
    """Write a message to the console and the run log."""
    global LOG_FILE_HANDLE
    line = message if message is not None else ""
    print(line)

    if LOG_FILE_HANDLE is not None:
        LOG_FILE_HANDLE.write(line + "\n")
        LOG_FILE_HANDLE.flush()


def initialize_run_log():
    """Create the timestamped log file used by this run."""
    global LOG_FILE_PATH, LOG_FILE_HANDLE
    os.makedirs(LOG_DIR, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    LOG_FILE_PATH = os.path.join(LOG_DIR, f"triggerv3_{stamp}.log")
    LOG_FILE_HANDLE = open(LOG_FILE_PATH, "a", encoding="utf-8", buffering=1)
    log(f"[LOG] Run log file: {LOG_FILE_PATH}")


def close_run_log():
    global LOG_FILE_HANDLE
    if LOG_FILE_HANDLE is not None:
        try:
            LOG_FILE_HANDLE.flush()
        finally:
            LOG_FILE_HANDLE.close()
            LOG_FILE_HANDLE = None


def describe_model_state(model_state):
    try:
        return RtlabApi.OP_MODEL_STATE(model_state)
    except AttributeError:
        state_map = {
            0: "NOT_CONNECTED",
            1: "NOT_LOADABLE",
            2: "COMPILING",
            3: "LOADABLE",
            4: "LOADING",
            5: "RESETTING",
            6: "LOADED",
            7: "PAUSED",
            8: "RUNNING",
            9: "DISCONNECTED",
        }
        return state_map.get(model_state, f"UNKNOWN_STATE ({model_state})")


def pack_float_little_endian(value):
    return struct.pack("<f", float(value))


def build_message_from_text_little_endian(text_bytes):
    payload = b"".join(pack_float_little_endian(byte) for byte in text_bytes)
    if len(payload) > 255:
        raise ValueError("text payload too large")
    return bytes([len(payload)]) + payload


def build_message_from_float_array(values):
    payload = b"".join(pack_float_little_endian(value) for value in values)
    if len(payload) > 255:
        raise ValueError("float payload too large")
    return bytes([len(payload)]) + payload


def decode_text_from_float_payload(message):
    if not message:
        return None

    payload_len = message[0]
    payload = message[1:1 + payload_len]
    if len(payload) != payload_len or payload_len % 4 != 0:
        return None

    decoded = bytearray()
    for index in range(0, payload_len, 4):
        value = struct.unpack("<f", payload[index:index + 4])[0]
        if value < 0.0 or value > 255.0:
            return None
        decoded.append(int(value))

    try:
        return decoded.decode("ascii")
    except UnicodeDecodeError:
        return None


def is_expected_ready_sender(from_addr):
    return from_addr[0] == EXPECTED_READY_IP and from_addr[1] == EXPECTED_READY_PORT


def send_udp_packet(sock, target, payload, label):
    sent = sock.sendto(payload, target)
    log(f"[UDP] Sent {label} ({sent} bytes) to {target[0]}:{target[1]}")


def get_optional_opal_metric():
    """Try to retrieve any overrun-style counter exposed by the installed API."""
    candidate_names = (
        "GetOverrunCount",
        "GetOverruns",
        "GetOverrunNumber",
        "GetSimulationOverruns",
        "GetModelOverrunCount",
        "GetTotalOverrunCount",
        "ATT_DETECT_OVERRUNS"
    )

    for name in candidate_names:
        candidate = getattr(RtlabApi, name, None)
        if callable(candidate):
            try:
                return name, candidate()
            except TypeError:
                continue
            except Exception as exc:
                return name, f"error: {exc}"

    return None, None


def log_model_snapshot(context):
    try:
        model_state, _ = RtlabApi.GetModelState()
        log(f"[OPAL] {context}: {describe_model_state(model_state)}")

        metric_name, metric_value = get_optional_opal_metric()
        if metric_name is not None:
            log(f"[OPAL] {metric_name}: {metric_value}")
        else:
            log("[OPAL] Overrun counter: unavailable in this RT-LAB API build.")

        return model_state
    except (OSError, RuntimeError, ValueError) as exc:
        log(f"[OPAL] {context}: unable to read model snapshot ({exc})")
        return None


def wait_for_ready_and_negotiate(sock, target):
    msg_dummy = build_message_from_text_little_endian(DUMMY_TEXT)
    msg_preset = build_message_from_float_array(PRESET_VALUES)
    start_time = time.perf_counter()
    last_resend_time = 0.0

    log(f"[NEG] Waiting for READY_ from {EXPECTED_READY_IP}:{EXPECTED_READY_PORT}...")
    log("[NEG] Press 'd' to send dummy message, 'p' to send preset float message, 'q' to quit.")

    while True:
        if time.perf_counter() - start_time > NEGOTIATION_TIMEOUT_SECONDS:
            log("[NEG] ERROR: Timeout waiting READY_.")
            return False

        if msvcrt is not None and msvcrt.kbhit():
            key = msvcrt.getch().decode("ascii", errors="ignore").lower()
            if key == "q":
                log("[NEG] Exiting on user request.")
                return False
            if key == "d":
                send_udp_packet(sock, target, msg_dummy, "dummy message")
            elif key == "p":
                send_udp_packet(sock, target, msg_preset, "preset float message")

        if time.perf_counter() - last_resend_time >= RESEND_PERIOD_SECONDS:
            last_resend_time = time.perf_counter()

        sock.settimeout(0.2)
        try:
            data, from_addr = sock.recvfrom(1024)
        except socket.timeout:
            continue

        log(f"[UDP] Received packet from {from_addr[0]}:{from_addr[1]}")
        log(f"[UDP] Bytes ({len(data)}): {' '.join(str(byte) for byte in data)}")

        decoded_text = decode_text_from_float_payload(data)
        if decoded_text is None:
            continue

        log(f"[UDP] Decoded text: {decoded_text}")
        if decoded_text == READY_TEXT.decode("ascii"):
            if is_expected_ready_sender(from_addr):
                log("[NEG] READY_ validated from expected endpoint.")
                return True

            log("[NEG] READY_ received but endpoint does not match expected sender.")
            return False


def wait_for_pause_state(timeout_seconds=60.0, poll_period=1.0):
    """Wait until RT-LAB reports the model is paused, showing live state logs."""
    deadline = time.perf_counter() + timeout_seconds

    log("[OPAL] Monitoring target node initialization progress...")
    while time.perf_counter() < deadline:
        try:
            model_state, _ = RtlabApi.GetModelState()
            log(f"[OPAL] Target status: {describe_model_state(model_state)}")

            if model_state == MODEL_PAUSED_STATE:
                return True
        except (OSError, RuntimeError, ValueError) as exc:
            log(f"[OPAL] Polling notice: waiting for subsystem response... ({exc})")

        time.sleep(poll_period)

    return False


def check_for_pause_state():
    """Check whether the model is running and attempt to pause it if needed."""
    try:
        model_state, _ = RtlabApi.GetModelState()
        log(f"[OPAL] Current model state: {describe_model_state(model_state)}")

        if model_state == MODEL_RUNNING_STATE:
            log("[OPAL] Model is currently RUNNING. Attempting to PAUSE...")
            RtlabApi.Pause()
            time.sleep(2)
            return True

        if model_state == MODEL_PAUSED_STATE:
            log("[OPAL] Model is already PAUSED.")
            return True

        log("[OPAL] Model is in an unexpected state. Expected RUNNING or PAUSED.")
        return False

    except (OSError, RuntimeError, ValueError) as exc:
        log(f"[OPAL] Error checking model state: {exc}")
        return False


def main():
    initialize_run_log()
    log(f"[OPAL] Opening project: {PROJECT_PATH}")

    try:
        log("[OPAL] Uploading/opening project on RT-LAB...")
        RtlabApi.OpenProject(PROJECT_PATH)
        log_model_snapshot("OpenProject response")

        log("[OPAL] Acquiring system control...")
        RtlabApi.GetSystemControl(1)
        log("[OPAL] System control requested.")

        log("[OPAL] Uploading model binaries to the target...")
        RtlabApi.Load(2, 1.0)
        log_model_snapshot("Load response")

        log("[OPAL] Acquiring monitoring control...")
        RtlabApi.GetMonitoringControl(1)
        log("[OPAL] Monitoring control requested.")

        if not check_for_pause_state():
            log("[OPAL] Pause request issued after unexpected state.")
            RtlabApi.Pause(1.0)

        if not wait_for_pause_state(timeout_seconds=60.0, poll_period=1.5):
            log("[ERROR] Target failed to enter a stable PAUSED state within 60s. Aborting.")
            RtlabApi.GetMonitoringControl(0)
            RtlabApi.GetSystemControl(0)
            RtlabApi.CloseProject()
            return
    

        log("[SUCCESS] Target mirrors the GUI state: PAUSED.")
        log("[STATUS] AsyncIP network drivers are running on target. Awaiting UDP trigger...")

        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind((UDP_IP, UDP_PORT))
        log(f"[UDP] Listening on port {UDP_PORT} for negotiation and trigger messages...")

        negotiation_target = (NEGOTIATION_TARGET_IP, NEGOTIATION_TARGET_PORT)

        try:
            got_ready = wait_for_ready_and_negotiate(sock, negotiation_target)
            if not got_ready:
                log("[OPAL] Execution canceled because READY_ was not validated.")
                return

            log("[OPAL] READY_ validated. Starting execution...")
            start_time = time.perf_counter()
            RtlabApi.Execute(1.0)
            end_time = time.perf_counter()

            latency_ms = (end_time - start_time) * 1000
            log(f"[OPAL] Simulation is now RUNNING. API trigger latency: {latency_ms:.2f} ms")
            log_model_snapshot("Post-execute snapshot")

            # Wait until the letter 'q' is pressed to quit the server
            print("\n[STATUS] Press 'q' to stop the server and close the project.")
            while True:
                if msvcrt is not None and msvcrt.kbhit():
                    key = msvcrt.getch().decode("ascii", errors="ignore").lower()
                    if key == "q":
                        print("Exiting server...")
                        break
                time.sleep(0.1)

        except (OSError, RuntimeError, ValueError) as exc:
            log(f"[EXCEPTION] An error occurred during automation: {exc}")

        finally:
            sock.close()
            RtlabApi.GetMonitoringControl(0)

            if not check_for_pause_state():
                log("[OPAL] Run appears active. Sending PAUSE before reset.")
                RtlabApi.Pause(1.0)
                time.sleep(1)

            log_model_snapshot("Pre-reset snapshot")
            log("[OPAL] Resetting target...")
            RtlabApi.Reset()

            log_model_snapshot("Post-reset snapshot")
            RtlabApi.GetSystemControl(0)
            RtlabApi.CloseProject()
            log("[OPAL] Run terminated. Project closed and API disconnected cleanly.")

    finally:
        close_run_log()

# ===============================================================================
if __name__ == "__main__":
    main()
