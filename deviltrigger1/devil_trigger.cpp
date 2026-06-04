#include <iostream>
#include <cstring>
#include <string>
#include <chrono>

// 1. Include the OPAL-RT C-API Header
// Usually located in: C:\OPAL-RT\RT-LAB\<Version>\common\include\OpalApi.h
#include "OpalApi.h"

// 2. Include Windows Sockets for UDP communication
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib") 
// NOTE: You must also add 'OpalApi.lib' to your IDE's linker dependencies.

const int UDP_PORT = 5008;
const std::string LAUNCH_CODE = "LAUNCH_SIM";
const char* PROJECT_PATH = "C:\\OPAL-RT\\Workspace\\MyProject\\MyProject.llp";

int main() {
    int ret;

    // --- PHASE 1: INITIALIZE OPAL-RT & PRE-LOAD ---
    std::cout << "[OPAL] Connecting to project: " << PROJECT_PATH << std::endl;
    ret = OpalOpenProject(PROJECT_PATH);
    if (ret != EOK) { // EOK is defined in OpalApi.h (typically 0)
        std::cerr << "Error: Failed to open project. Code: " << ret << std::endl;
        return 1;
    }

    // Define Real-Time Mode: 
    // 1 = SIM_MODE (Hardware sync), 2 = SOFT_SIM_MODE, 3 = VIRTUAL_SIM_MODE
    int realTimeMode = 1; 
    double timeFactor = 1.0;

    std::cout << "[OPAL] Pre-loading model binaries to target node..." << std::endl;
    std::cout << "       (This handles all heavy network transfers and memory allocation)" << std::endl;
    
    ret = OpalLoad(realTimeMode, timeFactor);
    if (ret != EOK) {
        std::cerr << "Error: Failed to load model. Code: " << ret << std::endl;
        OpalCloseProject();
        return 1;
    }
    std::cout << "[OPAL] SUCCESS: Model is pre-loaded and PAUSED on target. Standing by..." << std::endl;


    // --- PHASE 2: SETUP UDP LISTENER SOCKET ---
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Error: Winsock initialization failed." << std::endl;
        OpalCloseProject();
        return 1;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Error: Socket creation failed." << std::endl;
        WSACleanup();
        OpalCloseProject();
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(UDP_PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Error: Socket bind failed." << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        OpalCloseProject();
        return 1;
    }

    std::cout << "[UDP] Network server online. Listening on port " << UDP_PORT << "..." << std::endl;


    // --- PHASE 3: MONITOR & IMMEDIATE EXECUTION ---
    char buffer[1024];
    sockaddr_in clientAddr{};
    int clientAddrLen = sizeof(clientAddr);

    while (true) {
        int bytesReceived = recvfrom(serverSocket, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&clientAddr, &clientAddrLen);
        if (bytesReceived > 0) {
            buffer[bytesReceived] = '\0'; 
            std::string msg(buffer);
            
            // Strip trailing newlines or spaces common in UDP packets
            while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r' || msg.back() == ' ')) {
                msg.pop_back();
            }

            if (msg == LAUNCH_CODE) {
                std::cout << "[⚡] Match found! Sending immediate execution signal..." << std::endl;
                
                // Track latency of the API execution call
                auto startTime = std::chrono::high_resolution_clock::now();
                
                // This command instantly commands the already-loaded target node to run
                ret = OpalExecute(timeFactor);
                
                auto endTime = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

                if (ret == EOK) {
                    std::cout << "🚀 Simulation is now RUNNING! (Trigger latency: " << duration << " ms)" << std::endl;
                } else {
                    std::cerr << "Error: Failed to execute model. Code: " << ret << std::endl;
                }
                break; // Break loop to proceed to cleanup
            } else {
                std::cout << "[UDP] Ignored unknown payload: " << msg << std::endl;
            }
        }
    }

    // --- PHASE 4: CLEANUP ---
    closesocket(serverSocket);
    WSACleanup();
    OpalCloseProject();
    std::cout << "[OPAL] Session disconnected cleanly." << std::endl;

    return 0;
}