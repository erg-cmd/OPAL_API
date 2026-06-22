import os 
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

# Read the log file
log_file_path = r"C:\Users\elias.gracia\Documents\API_OPAL\plots\discon2udp_log.txt"

# Split the data into df_send and df_receive

# if the line contains "sent:" then it is a send line, the data after that is the data sent
# if the line contains "recieve:" then it is a receive line, the data after that is the data received

# data sent line contains Yaw, GenSpeed, TimeSim, HorzWind, YawError and BldPitch
# data received line contains TimeOpal, TimeSim, BldPitchColl, BldPitch1, Telect and YawCmd



for line in open(log_file_path):
    if "sent:" in line:
        data = line.split("sent:")[1].strip()
        # 
        


send_data = []