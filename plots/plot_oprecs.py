""" 
This  script reads an OPREC file and plots the signal contained in it.
"""
from __future__ import print_function
import sys
import os
# import pandas as pd
import matplotlib.pyplot as plt
import DataloggerApi as dlapi
if sys.version_info[0] < 3:
    import Tkinter
    import tkFileDialog
else: #Python 3 and up
    import tkinter
    import tkinter.filedialog

READ_BUFFER = 1  # in seconds
TS_DECIMAL_PRECISION = 6   # in microseconds


def select_oprec_file():
    # Pops file browser dialog to allow selection of a .oprec file, and return file path
    if sys.version_info[0] < 3:
        root = Tkinter.Tk()
    else: #Python 3 and up
        root = tkinter.Tk()
    root.withdraw()
    root.attributes('-topmost', True)
    if sys.version_info[0] < 3:
        file_path = tkFileDialog.askopenfilename(filetypes=(('OPAL-RT recording files', '*.oprec'), ('All files', '*.*')))
    else: #Python 3 and up
        file_path = tkinter.filedialog.askopenfilename(filetypes=(('OPAL-RT recording files', '*.oprec'), ('All files', '*.*')))
    return str(os.path.normpath('\\'.join(file_path.split('/'))))


# -----------------------------------------------------------------------------
#  Script core
# -----------------------------------------------------------------------------
# if the script is executed (not imported)
if __name__ == '__main__':

    # We will read a .oprec file    
    print('Please select a .oprec file from the dialog box.')
    oprec_file_path = select_oprec_file()
    if not oprec_file_path:
        print('No file was selected.')
        sys.exit()

    print('Selected file: %s' % oprec_file_path)

    signal_group = dlapi.SignalGroup(oprec_file_path)

    signal_paths = [info.get_source() for info in signal_group.get_signals_info()]
    time_step = signal_group.get_step_info().get_time_step()

    print('--------------------')
    print('Found {} signals in selected file: {}'.format(len(signal_paths)-1, signal_paths))
    print('Step size in nanoseconds: {}'.format(time_step))

    frame_config = dlapi.FrameConfig(start_time_ns=0, step_count=int(READ_BUFFER * 1e9 / time_step + 0.5))
    
    # Read the whole file in chunks of READ_BUFFER seconds, and plot the first 10 values of each signal for each chunk
    
    
    
    min_frames_with_data = 100
    max_frame_to_test = 100
    frames_read = 0
    frames_with_data_read = 0
    while (frames_with_data_read != min_frames_with_data
            and frames_read < max_frame_to_test):
        result = signal_group.configure_frame(frame_config)
        print('\n\n----  Will print data from frame', (frames_read + 1), '  ----')

        try:
            frame = signal_group.get_sub_frame()
            signal_values = frame.get_signals_data()

            timestamps = frame.get_timestamps()
            timestamps_size = len(timestamps)
            print('\nNumber of timestamps in the frame:', timestamps_size)
            if timestamps_size == 0:
                print('The selected file has no data for this frame.')
                sys.exit()

            print('Number of signals for which there are values:', len(signal_values)-1)
            print('Number of values for 1st signal:', len(signal_values[0]))

            n = min(timestamps_size, 10)
            print('\nWill plot values of all signals, for first {} timestamps.\n'.format(n))
            print('Time:                   ', '\t'.join(map(str, timestamps[:n])))

            # print('Values:')
            # get the number of signals and their values, and plot them in subplots
            num_signals = len(signal_values)
            fig, axs = plt.subplots(num_signals, 1, figsize=(10, 2*num_signals))
            
            for path, values in zip(signal_paths, signal_values):
                # print(path, '     ', '\t'.join(map(str, values[:n])))
                axs[signal_paths.index(path)].plot(timestamps[:n], values[:n], label=path)
                axs[signal_paths.index(path)].set_xlabel('Time')
                axs[signal_paths.index(path)].set_ylabel('Signal Value')
                axs[signal_paths.index(path)].set_title('OPAL-RT Signal Plot')
                axs[signal_paths.index(path)].legend()
            
            # plot show
            plt.tight_layout()
            plt.show()
            

            frames_with_data_read += 1

        except:
            print('Cannot print anything because in this case it was impossible to get data for the specified configuration.')

        # prepare for next read by configuring a different start time
        if sys.version_info[0] < 3:
            frame_config.set_start_time(frame_config.get_start_time() + long(READ_BUFFER * 1e9))
        else: #Python 3 and up
            frame_config.set_start_time(frame_config.get_start_time() + int(READ_BUFFER * 1e9))
        frames_read += 1