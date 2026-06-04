# -----------------------------------------------------------------------------
# This example shows how to use the Python API to make a connection
# to a current running (or paused) model. It also shows how to
# change the current state of the model using the pause and
# execute command. In this simple example, the current model state
# is toggled between paused and executed
#
# WARNING: Before runinng this script, verify that the model is compiled and is
# running (or paused)
# -----------------------------------------------------------------------------


# -----------------------------------------------------------------------------
#  Import modules
# -----------------------------------------------------------------------------
## Import OpalApi module for Python
import RtlabApi
import glob
import os


# -----------------------------------------------------------------------------
#  Script core
# -----------------------------------------------------------------------------
## if the script is executed (not imported)
if __name__ == "__main__":

    ## Connect to a running model using its name. The system
    ## control is release     
    projectName = os.path.abspath(str(glob.glob('.\\..\\*.llp')[0]))
    RtlabApi.OpenProject(projectName)
    
    print("The connection with '%s' is completed." % projectName)

    try:
        ## Get the model state and the real time mode
        modelState, realTimeMode = RtlabApi.GetModelState()
                
        ## Print the model state
        print("- The model state is %s." % RtlabApi.OP_MODEL_STATE(modelState))
        
        ## Get the system control before changing the state of the model
        systemControl = 1
        RtlabApi.GetSystemControl(systemControl)
        print("- The system control is acquired.")

        ## If the model is running
        if modelState == RtlabApi.MODEL_RUNNING:
            ## Pause the model
            RtlabApi.Pause()
           
        ## if the model is not running
        else:
            ## Execute the model
            RtlabApi.Execute(1)

        ## Get the model state and the real time mode
        modelState, realTimeMode = RtlabApi.GetModelState()

        ## Print the model state
        print("- The model state is now %s." % RtlabApi.OP_MODEL_STATE(modelState))

        ## Release the system control after changing the state of the model
        systemControl = 0
        RtlabApi.GetSystemControl(systemControl)
        print("- The system control is released.")

    finally:
        ## Always disconnect from the model when the connection
        ## is completed
        RtlabApi.CloseProject()
        print("The connection is closed.")
