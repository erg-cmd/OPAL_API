import socket
import sys
import time

# --- OPAL-RT API PATH CONFIGURATION ---
# Adjust this path to match your specific RT-LAB installation version
RTLAB_PATH = r"C:\OPAL-RT\RT-LAB\2021.3\common\python" 
if RTLAB_PATH not in sys.path:
    sys.path.append(RTLAB_PATH)

try:
    import RtlabApi
except ImportError:
    print(f"Error: Could not find RtlabApi at {RTLAB_PATH}. Check your path settings.")
    sys.path.append(r"C:\OPAL-RT\RT-LAB\2021.3\common\python") # Fallback attempt
    import RtlabApi

# --- NETWORK & PROJECT PARAMETERS ---
UDP_IP = "0.0.0.0"          # Listen on all local network adapters
UDP_PORT = 5008             # Port on Windows PC waiting for the trigger code
LAUNCH_CODE = b"LAUNCH_SIM" # Byte-string trigger payload

PROJECT_PATH = r"C:\Archivos_INI_OPAL\ergs_test5\ergs_test5.llp" 


def wait_for_pause_state(timeout_seconds=10.0, poll_period=0.5):
    """Wait until RT-LAB reports the model is paused and ready."""
    deadline = time.perf_counter() + timeout_seconds

    while time.perf_counter() < deadline:
        model_state, _ = RtlabApi.GetModelState()
        # State 2 corresponds to MODEL_PAUSE (target loaded and waiting).
        if model_state == 2:
            return True
        time.sleep(poll_period)

    return False

def main():
    # 1. Open the project and preload the model.
    print(f"[OPAL] Opening project: {PROJECT_PATH}")
    RtlabApi.OpenProject(PROJECT_PATH)

    print("[OPAL] Pre-loading model binaries to target...")
    RtlabApi.Load(2, 1.0)

    print("[OPAL] Polling target state. Waiting for PAUSE mode...")
    if not wait_for_pause_state():
        print("[ERROR] Target failed to enter a stable PAUSE state. Aborting.")
        RtlabApi.Disconnect()
        return

    print("\n[SUCCESS] Target mirrors the GUI state: PAUSED.")
    print("[STATUS] Awaiting UDP trigger...\n")

    # 2. Initialize UDP Socket.
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))
    print(f"[UDP] Listening for trigger '{LAUNCH_CODE.decode()}' on port {UDP_PORT}...")

    try:
        # 3. Wait for the external network trigger.
        while True:
            data, addr = sock.recvfrom(1024)
            clean_data = data.strip()
            
            if clean_data == LAUNCH_CODE:
                print(f"[⚡] Trigger received from {addr}! Firing immediate execution...")
                
                # Command the preloaded target to instantly start calculations
                start_time = time.perf_counter()
                RtlabApi.Execute(1.0)
                end_time = time.perf_counter()
                
                latency_ms = (end_time - start_time) * 1000
                print(f"🚀 Simulation is now RUNNING! (API trigger latency: {latency_ms:.2f} ms)")
                break
            else:
                print(f"[UDP] Ignored unknown packet content: {clean_data}")

    except (OSError, RuntimeError, ValueError) as e:
        print(f"\n[EXCEPTION] An error occurred during automation: {e}")
        
    finally:
        # 4. Clean up communication paths.
        sock.close()
        RtlabApi.Disconnect()
        print("[OPAL] API disconnected cleanly.")

if __name__ == "__main__":
    main()