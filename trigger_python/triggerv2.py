import socket
import struct
import sys
import time

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
UDP_IP = "0.0.0.0"          # Listen on all local network adapters
UDP_PORT = 5008             # Port on Windows PC waiting for the trigger code
LAUNCH_CODE = b"LAUNCH_SIM" # Byte-string trigger payload
PROJECT_PATH = r"C:\Archivos_INI_OPAL\ergs_test5\ergs_test5.llp"
NEGOTIATION_TARGET_IP = "192.168.10.123"
NEGOTIATION_TARGET_PORT = 5008
EXPECTED_READY_IP = "192.168.10.123"
EXPECTED_READY_PORT = 5008
READY_TEXT = b"READY_"
DUMMY_TEXT = b"ByeBye"
PRESET_VALUES = [1.0, 0.005, 30.0, 1.0, 2.0, 10.0]
NEGOTIATION_TIMEOUT_SECONDS = 60.0
RESEND_PERIOD_SECONDS = 1.0


def describe_model_state(model_state):
    try:
        return RtlabApi.OP_MODEL_STATE(model_state)
    except AttributeError:
        state_map = {
            0: "NOT_INITIALIZED / UNLOADED",
            1: "MODEL_LOADABLE (Ready to Load)",
            2: "PAUSED (Loaded & Ready to Execute)",
            3: "RUNNING",
            4: "RESETTING",
            5: "PAUSING",
            6: "STOPPING",
            7: "ERROR",
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
    print(f"[UDP] Sent {label} ({sent} bytes) to {target[0]}:{target[1]}")


def wait_for_ready_and_negotiate(sock, target):
    msg_dummy = build_message_from_text_little_endian(DUMMY_TEXT)
    msg_preset = build_message_from_float_array(PRESET_VALUES)
    start_time = time.perf_counter()

    print(f"[NEG] Waiting for READY_ from {EXPECTED_READY_IP}:{EXPECTED_READY_PORT}...")
    print("[NEG] Press 'd' to send dummy message, 'p' to send preset float message, 'q' to quit.")

    while True:
        if time.perf_counter() - start_time > NEGOTIATION_TIMEOUT_SECONDS:
            print("[NEG] ERROR: Timeout waiting READY_.")
            return False

        if msvcrt is not None and msvcrt.kbhit():
            key = msvcrt.getch().decode("ascii", errors="ignore").lower()
            if key == "q":
                print("Exiting server...")
                return False
            if key == "d":
                send_udp_packet(sock, target, msg_dummy, "dummy message")
            elif key == "p":
                send_udp_packet(sock, target, msg_preset, "preset float message")

        sock.settimeout(0.2)
        try:
            data, from_addr = sock.recvfrom(1024)
        except socket.timeout:
            continue

        print(f"[UDP] Received packet from {from_addr[0]}:{from_addr[1]}")
        print(f"[UDP] Bytes ({len(data)}): {' '.join(str(byte) for byte in data)}")

        decoded_text = decode_text_from_float_payload(data)
        if decoded_text is None:
            continue

        print(f"[UDP] Decoded text: {decoded_text}")
        if decoded_text == READY_TEXT.decode("ascii"):
            if is_expected_ready_sender(from_addr):
                print("[NEG] READY_ validated from expected endpoint.")
                return True

            print("[NEG] READY_ received but endpoint does not match expected sender.")
            return False


def wait_for_pause_state(timeout_seconds=60.0, poll_period=1.0):
    """Waits until RT-LAB reports the model is paused, showing live state logs."""
    deadline = time.perf_counter() + timeout_seconds

    print(" -> Monitoring target node initialization progress...")
    while time.perf_counter() < deadline:
        try:
            model_state, _ = RtlabApi.GetModelState()
            print(f"   [Target Status]: {describe_model_state(model_state)}")

            if model_state == 7:  # MODEL_PAUSED
                return True
        except (OSError, RuntimeError, ValueError) as e:
            print(f"   [Polling Notice]: Waiting for subsystem response... ({e})")

        time.sleep(poll_period)

    return False

def check_for_pause_state():
    """Checks if the model is currently running and attempts to pause it."""
    try:
        model_state, _ = RtlabApi.GetModelState()
        print(f"[OPAL] Current model state: {describe_model_state(model_state)}")

        if model_state == 8:  # MODEL_RUNNING
            print("[OPAL] Model is currently RUNNING. Attempting to PAUSE...")
            RtlabApi.Pause()
            time.sleep(2)  # Give it a moment to transition states
            return True

        if model_state == 7:  # MODEL_PAUSED
            print("[OPAL] Model is already PAUSED.")
            return True

        print("[OPAL] Model is in an unexpected state. Expected RUNNING or PAUSED.")
        return False

    except (OSError, RuntimeError, ValueError) as e:
        print(f"[OPAL] Error checking model state: {e}")
        return False


def main():
    print(f"[OPAL] Opening project: {PROJECT_PATH}")
    RtlabApi.OpenProject(PROJECT_PATH)
    model_state, _ = RtlabApi.GetModelState()
    print(f"[OPAL] OpenProject response: {describe_model_state(model_state)}")

    print("[OPAL] Acquiring system control...")
    RtlabApi.GetSystemControl(1)
    print("[OPAL] System control requested.")

    print("[OPAL] Pre-loading model binaries...")
    RtlabApi.Load(1, 1.0)
    model_state, _ = RtlabApi.GetModelState()
    print(f"[OPAL] Load response: {describe_model_state(model_state)}")

    print("[OPAL] Acquiring monitoring control...")
    RtlabApi.GetMonitoringControl(1)
    print("[OPAL] Monitoring control requested.")
    
    # check if the model is running and pause it
    if not check_for_pause_state():  # MODEL_RUNNING
        RtlabApi.Pause(1.0)

    if not wait_for_pause_state(timeout_seconds=60.0, poll_period=1.5):
        print("\n[ERROR] Target failed to enter a stable PAUSE state within 60s. Aborting.")
        RtlabApi.GetMonitoringControl(0)
        RtlabApi.GetSystemControl(0)
        RtlabApi.CloseProject()
        return

    print("\n[SUCCESS] Target perfectly mirrors the GUI state: PAUSED.")
    print("[STATUS] AsyncIP network drivers are running on target. Awaiting UDP trigger...\n")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))
    print(f"[UDP] Listening on port {UDP_PORT} for negotiation and trigger messages...")

    negotiation_target = (NEGOTIATION_TARGET_IP, NEGOTIATION_TARGET_PORT)

    try:
        got_ready = wait_for_ready_and_negotiate(sock, negotiation_target)
        if not got_ready:
            print("[OPAL] Execution canceled because READY_ was not validated.")
            return

        print("[OPAL] READY_ validated. Starting execution...")
        start_time = time.perf_counter()
        RtlabApi.Execute(1.0)
        end_time = time.perf_counter()

        latency_ms = (end_time - start_time) * 1000
        print(f"🚀 Simulation is now RUNNING! (API trigger latency: {latency_ms:.2f} ms)")
        
        # Sleep for a short while to allow the simulation to run before cleanup
        time.sleep(31)

    except (OSError, RuntimeError, ValueError) as e:
        print(f"\n[EXCEPTION] An error occurred during automation: {e}")

    finally:
        sock.close()
        RtlabApi.GetMonitoringControl(0)
        RtlabApi.GetSystemControl(0)
        RtlabApi.CloseProject()
        print("[OPAL] Project closed and API disconnected cleanly.")


if __name__ == "__main__":
    main()