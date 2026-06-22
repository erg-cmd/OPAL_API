## OFFLINE vs HIL COMPARISON
# This scripts compares GenSpeed, GenTorque, GenPower, PitchAngle, Vdc_link

import os
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from sklearn.metrics import mean_squared_error, mean_absolute_error, r2_score
from scipy.interpolate import interp1d

# === 0. Definición de referencias y rutas
# this_dir = os.getcwd()
# rel_path_offline = "OpenFAST4\ergs\pwrsyst2\wt5mw_001\sim\Registro_SIM.csv"
# rel_path_offline = rel_path_offline.replace("/", os.sep)
# rel_path_hil = "Raspy_WT/SIGNAL_GROUP_1_commasync_1_sm_computation_20260617_015816.csv"
# rel_path_hil = rel_path_hil.replace("/", os.sep)


folder_path_hil = r"C:\Archivos_INI_OPAL\ergs_test5\data"
# filename_hil = "SIGNAL_GROUP_1_commasync_1_sm_computation_20260617_015816.csv"

# list files in the folder and find the most recent CSV file
csv_files = [f for f in os.listdir(folder_path_hil) if f.endswith(".csv")]

# sort the files by modification time and get the most recent one
csv_files.sort(key=lambda x: os.path.getmtime(os.path.join(folder_path_hil, x)), reverse=True)
# get the most recent file
filename_hil = csv_files[0] if csv_files else None
filename_hil_prev = csv_files[1] if len(csv_files) > 1 else None


COLUMNS_TO_COMPARE = [
    "Tem_Nm",
    "Wgen_rpm",
    "Pgen_W",
    "Vdc_V",
    "Pitch_deg",
]

COLUMNS_UNITS = {
    "Tem_Nm": "Nm",
    "Wgen_rpm": "rpm",
    "Pgen_W": "W",
    "Vdc_V": "V",
    "Pitch_deg": "deg",
}

ADJUST_TO_HEAD = (
    True  # aligns to the head of the smaller DataFrame if True, else aligns to the tail
)

TIME_STEP = 20e-6  # 50 microseconds, adjust as necessary for your data

# file_path_offline = os.path.join(this_dir, rel_path_offline)
# file_path_hil = os.path.join(this_dir, rel_path_hil)

file_path_hil = os.path.join(folder_path_hil, filename_hil) if filename_hil else None
file_path_hil_prev = os.path.join(folder_path_hil, filename_hil_prev) if filename_hil_prev else None

# === 1. Cargar datos ===
try:
    # df_offline = pd.read_csv(file_path_offline)
    df_hil = pd.read_csv(file_path_hil)
    df_hil_prev = pd.read_csv(file_path_hil_prev) if filename_hil_prev else None
    print("✅ DataFrames loaded successfully.")
except FileNotFoundError as e:
    print(f"❌ Error loading files: {e}")
    exit()
except Exception as e:
    print(f"❌ Unexpected error during file loading: {e}")
    exit()

# === 2. Validación básica ===
# if df_offline.empty or df_hil.empty:
#     print("❌ One or both DataFrames are empty after loading. Exiting.")
#     exit()

df_hil = df_hil.rename(
    columns={
        "sm_computation/Gain2/port1[0]": "Tem_Nm",
        "sm_computation/Gain2/port1[1]": "Wgen_rpm",
        "sm_computation/Gain2/port1[2]": "Pgen_W",
        "sm_computation/Gain2/port1[3]": "Vdc_V",
        "sm_computation/Gain2/port1[4]": "Pitch_deg",
    }
)
df_hil_prev = df_hil_prev.rename(
    columns={
        "sm_computation/Gain2/port1[0]": "Tem_Nm",
        "sm_computation/Gain2/port1[1]": "Wgen_rpm",
        "sm_computation/Gain2/port1[2]": "Pgen_W",
        "sm_computation/Gain2/port1[3]": "Vdc_V",
        "sm_computation/Gain2/port1[4]": "Pitch_deg",
    }
)

# if the first row of the DataFrame is a header or metadata, drop it
if df_hil.iloc[0].dtype == object:
    df_hil = df_hil.iloc[1:].reset_index(drop=True)
if df_hil_prev.iloc[0].dtype == object:
    df_hil_prev = df_hil_prev.iloc[1:].reset_index(drop=True)

# === Sanity checks for column existence: check the size and existence of the columns
required_columns_hil = ["Tem_Nm", "Wgen_rpm", "Pgen_W", "Vdc_V", "Pitch_deg"]

if not all(col in df_hil.columns for col in required_columns_hil):
    print(
        f"❌ Missing required columns in HIL DataFrame. Found columns: {df_hil.columns}"
    )
    exit()
if not all(col in df_hil_prev.columns for col in required_columns_hil):
    print(
        f"❌ Missing required columns in HIL Previous DataFrame. Found columns: {df_hil_prev.columns}"
    )
    exit()
    
# Check size of DataFrames, dataframes must match in size, if not, crop the bigger one to the size of the smaller one
if df_hil_prev.shape[0] != df_hil.shape[0]:

    min_size = min(df_hil_prev.shape[0], df_hil.shape[0])

    if ADJUST_TO_HEAD:

        df_hil_prev = df_hil_prev.head(min_size)
        df_hil = df_hil.head(min_size)
    else:
        df_hil_prev = df_hil_prev.tail(min_size)
        df_hil = df_hil.tail(min_size)


# Subplot for each column comparison
plt.figure(figsize=(12, 8))

# Create a time vector based on the number of samples and the time step
# time_offline = np.arange(len(df_offline)) * TIME_STEP
time_hil = np.arange(len(df_hil)) * TIME_STEP
time_hil_prev = np.arange(len(df_hil_prev)) * TIME_STEP

# === 5. Calculate metrics for each column ===
for col in COLUMNS_TO_COMPARE:
    y_meas = df_hil[col]
    y_meas_prev = df_hil_prev[col]

    plt.subplot(2, 3, COLUMNS_TO_COMPARE.index(col) + 1)    
    plt.plot(time_hil_prev, y_meas_prev, label="HIL Previous", color="green")
    plt.plot(time_hil, y_meas, label="HIL Measurement", color="orange")
    plt.title(f"{col} Comparison")
    plt.xlabel("Time (s)")
    plt.ylabel(f"{col} ({COLUMNS_UNITS.get(col, '')})")

    # set legend at the bottom right corner of the subplot
    plt.legend(loc="lower right")

plt.tight_layout()
plt.show()

# ___ EOF ___
