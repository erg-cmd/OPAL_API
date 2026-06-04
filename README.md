# OPAL_API
## Description


### trigger.py
Just a draft, a guideline for what comes next; triggerv2 takes from devil_trigger2.cpp
handshake and negotiation to stablish connection, handle the project and pause the executtion.
Awaits for the pressed key to send dummy message and preset message. Once the "READY_"
message is received, running state is set and waits 31 seconds until the OpenFAST code ends.

<!--  -->
### deviltrigger3:
A fully working script, solving the communication and managing correctly
the pause/stop state for OPAL. Version reinterpreted from "triggerv2.py".

### deviltrigger2:
Working only the negotiation and sends the start signal to OPAL

### deviltrigger1:
Just a reference for the next script

### example:
Copied from OPAL examples, just a reference.