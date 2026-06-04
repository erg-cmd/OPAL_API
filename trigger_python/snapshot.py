# -----------------------------------------------------------------------------
# This example shows how to use the snapshot to restore the initial state
# of a model after a simulation using the Python RT-LAB API.
#
# WARNING: Before runinng this script, verify that the model is compiled 
# -----------------------------------------------------------------------------


# -----------------------------------------------------------------------------
#  Import modules
# -----------------------------------------------------------------------------
## Import OpalApi module for Python
import sys
import glob
import os
from time import sleep

import RtlabApi

# -----------------------------------------------------------------------------
#  Script core
# -----------------------------------------------------------------------------
## if the script is executed (not imported)
if __name__ == "__main__":

    projectName = os.path.abspath(str(glob.glob('.\\..\\*.llp')[0]))
    RtlabApi.OpenProject(projectName)
    print("The connection with '%s' is completed." % projectName)

    modelState, realTimeMode = RtlabApi.GetModelState()
    
    try:
        ## Load the current model
        realTimeMode = RtlabApi.SIM_MODE  # Also possible to use HARD_SYNC_MODE, SOFT_SIM_MODE, SIM_W_NO_DATA_LOSS_MODE or SIM_W_LOW_PRIO_MODE
        timeFactor   = 1
        RtlabApi.Load(realTimeMode, timeFactor)
        print("- The model is loaded.")

        ## Get monitoring control before using snapshot
        monitoringControl = 1
        RtlabApi.GetMonitoringControl(monitoringControl)

        ## Take a snapshot of the model at step 0 of the simulation
        cmd        = 1     ## 1 = take snapshot, 2 = restore snapshot
        filename   = 'snapshotfile'
        overwrite  = 1
        increment  = 0 
        comment    = ''
        commentLen = 0
        RtlabApi.Snapshot(cmd, filename, overwrite, increment, comment, commentLen)
        print("- The snapshot was take at time 0.0 [s].")
            
        try:
            ## Execute the model
            RtlabApi.Execute(1)

            ## The model is executed 5 seconds
            sleepTime = 2
            sleep(sleepTime)
            print("- The model is executed during %f [s]." % sleepTime)

            ## Pause the model
            RtlabApi.Pause()
            print("- The model is paused.")

            ## Restore snapshot 
            cmd        = 2     ## 1 = take snapshot, 2 = restore snapshot
            RtlabApi.Snapshot(cmd, filename, overwrite, increment, comment, commentLen)
            print("- The snapshot is restored.")

            ## Execute the model
            RtlabApi.Execute()

            ## The model is executed 5 seconds
            sleep(sleepTime)
            print("- The model is executed again during %f [s] from simulation time 0.0 [s]." % \
                  sleepTime)
        except:
            pass

        ## Get monitoring control before using snapshot
        monitoringControl = 0
        RtlabApi.GetMonitoringControl(monitoringControl)

        ## Get the model state and the real time mode
        modelState, realTimeMode = RtlabApi.GetModelState()

        ## If the model is running or is paused
        if modelState in [RtlabApi.MODEL_PAUSED, RtlabApi.MODEL_RUNNING]:
            RtlabApi.Reset()
            print("- The model is reseted.")

    finally:
        ## Always disconnect from the model when the connection
        ## is completed
        RtlabApi.CloseProject()
        print("The connection is closed.")