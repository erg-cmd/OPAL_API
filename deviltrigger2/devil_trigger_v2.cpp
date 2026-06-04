/**----------------------------------------
 * @file devil_trigger_v2.cpp
 * @author Elias R. Gracia (elias.gracia@uah.es)
 * @brief This code works with ergs_test5.lpp, but cant start the UDP negotitation,
 * so a better management needs to be done. Only the load of the model is replied correctly,
 * then the Opal, executes the model but dont send and receives data to the UDP. The Raspberry5
 * sends the "READY_" message and sends data to OPAL that is not replied....
 * @version 0.1
 * @date 2026-06-04
 * @state Finished, but not fully working
 * @copyright Copyright (c) 2026
 * 
 */

#include <iostream>
#include <string>
#include <cstring>
#include <chrono>
#include <conio.h>

/*
Build notes:
1) MSVC example:
    cl /EHsc devil_trigger_v2.cpp /I"C:\OPAL-RT\RT-LAB\2021.3.4\common\include" ^
        /link /LIBPATH:"C:\OPAL-RT\RT-LAB\2021.3.4\common\lib" OpalApi.lib ws2_32.lib

2) MinGW g++ example (adjust paths and import library name if needed):
    g++ devil_trigger_v2.cpp -o devil_trigger_v2.exe -I"C:\OPAL-RT\RT-LAB\2021.3.4\common\include" -L"C:\OPAL-RT\RT-LAB\2021.3.4\common\lib" -lOpalApi -lws2_32
*/

#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "OpalApi.lib")

#include "OpalApi.h"

// const char* PROJECT_PATH = "C:\\Archivos_INI_OPAL\\ergs_test5\\models\\CommAsync\\ergs_test5.llp";
const char* PROJECT_PATH = "C:/Archivos_INI_OPAL/ergs_test5/ergs_test5.llp";
// const char* PROJECT_PATH = "C:/Archivos_INI_OPAL/ergs_api_test0/ergs_api_test0.llp";

const char* LOCAL_LISTEN_IP = "192.168.10.56";
const int LOCAL_LISTEN_PORT = 5008;

const char* NEGOTIATION_TARGET_IP = "192.168.10.123";
const int NEGOTIATION_TARGET_PORT = 5008;

const char* EXPECTED_READY_IP = "192.168.10.123";
const int EXPECTED_READY_PORT = 5008;

const char* READY_TEXT = "READY_";
const char* DUMMY_TEXT = "ByeBye";
const float PRESET_VALUES[6] = {1.0f, 0.005f, 30.0f, 1.0f, 2.0f, 10.0f};

const int NEGOTIATION_TIMEOUT_MS = 60000;
const int RESEND_PERIOD_MS = 1000;


enum REAL_TIME_MODE {
    HardSyncMode  = 0, // Hardware synchronization mode
    SimMode  = 1, // Simulation as fast as possible mode.
    SoftSimMode  = 2, // Software synchronization mode.
    SimWNoDataLossMode = 3, // Not used anymore
    SimWLowPrioMode  = 4 // Simulation as fast as possible
};

/************************************************************************/
// Function declarations



void PrintError(int rc, char *funcName)
{
	char			buf[512];
	unsigned short	len;

	OpalGetLastErrMsg(buf, sizeof(buf), &len);
	printf("Error %s (code %d) from %s\n", buf, rc, funcName);
	system("pause");
}

static void pack_float_little_endian(float value, unsigned char out[4]) {
    union {
        float f;
        unsigned int u;
    } cvt;

    cvt.f = value;
    out[0] = (unsigned char)(cvt.u & 0xFFU);
    out[1] = (unsigned char)((cvt.u >> 8) & 0xFFU);
    out[2] = (unsigned char)((cvt.u >> 16) & 0xFFU);
    out[3] = (unsigned char)((cvt.u >> 24) & 0xFFU);
}

static double unpack_little_endian_to_float(const unsigned char in[4]) {
    unsigned int u = ((unsigned int)in[0]) |
                     ((unsigned int)in[1] << 8) |
                     ((unsigned int)in[2] << 16) |
                     ((unsigned int)in[3] << 24);
    union {
        unsigned int u;
        float f;
    } cvt;

    cvt.u = u;
    return (double)cvt.f;
}

static int build_message_from_text_little_endian(const char* text, unsigned char* out_msg, int out_cap) {
    if (text == NULL || out_msg == NULL) {
        return -1;
    }

    int text_len = (int)std::strlen(text);
    int payload_len = text_len * 4;
    int total_len = payload_len + 1;

    if (payload_len > 255 || total_len > out_cap) {
        return -1;
    }

    out_msg[0] = (unsigned char)payload_len;
    for (int i = 0; i < text_len; ++i) {
        pack_float_little_endian((float)(unsigned char)text[i], &out_msg[1 + (i * 4)]);
    }

    return total_len;
}

static int build_message_from_float_array(const float* values, int count, unsigned char* out_msg, int out_cap) {
    int payload_len = count * 4;
    int total_len = payload_len + 1;

    if (values == NULL || out_msg == NULL || payload_len > 255 || total_len > out_cap) {
        return -1;
    }

    out_msg[0] = (unsigned char)payload_len;
    for (int i = 0; i < count; ++i) {
        pack_float_little_endian(values[i], &out_msg[1 + (i * 4)]);
    }

    return total_len;
}

static int decode_text_from_float_payload(const unsigned char* msg, int msg_len, char* out_text, int out_cap) {
    if (msg == NULL || out_text == NULL || msg_len < 1 || out_cap < 1) {
        return -1;
    }

    int payload_len = (int)msg[0];
    if (msg_len < 1 + payload_len || (payload_len % 4) != 0) {
        return -1;
    }

    int text_len = payload_len / 4;
    if (text_len + 1 > out_cap) {
        return -1;
    }

    for (int i = 0; i < text_len; ++i) {
        double v = unpack_little_endian_to_float(&msg[1 + (i * 4)]);
        if (v < 0.0 || v > 255.0) {
            return -1;
        }
        out_text[i] = (char)((unsigned char)((int)v));
    }

    out_text[text_len] = '\0';
    return text_len;
}

static void print_bytes(const unsigned char* data, int len) {
    std::cout << "[UDP] Bytes (" << len << "): ";
    for (int i = 0; i < len; ++i) {
        std::cout << (unsigned int)data[i] << " ";
    }
    std::cout << std::endl;
}

static bool is_expected_ready_sender(const sockaddr_in& from) {
    if (ntohs(from.sin_port) != EXPECTED_READY_PORT) {
        return false;
    }

    unsigned long expected_ip = inet_addr(EXPECTED_READY_IP);
    if (expected_ip == INADDR_NONE) {
        return false;
    }

    return from.sin_addr.s_addr == expected_ip;
}

static bool send_udp_packet(SOCKET sd, const sockaddr_in& target, const unsigned char* msg, int len, const char* label) {
    int sent = sendto(sd, (const char*)msg, len, 0, (const sockaddr*)&target, sizeof(target));
    if (sent == SOCKET_ERROR) {
        std::cerr << "[UDP] ERROR: sendto failed for " << label << ", WSA=" << WSAGetLastError() << std::endl;
        return false;
    }

    std::cout << "[UDP] Sent " << label << " (" << sent << " bytes) to "
              << NEGOTIATION_TARGET_IP << ":" << NEGOTIATION_TARGET_PORT << std::endl;
    return true;
}

static bool wait_for_ready_and_negotiate(SOCKET sd, const sockaddr_in& target) {
    unsigned char msg_dummy[256];
    unsigned char msg_preset[256];

    int msg_dummy_len = build_message_from_text_little_endian(DUMMY_TEXT, msg_dummy, (int)sizeof(msg_dummy));
    int msg_preset_len = build_message_from_float_array(PRESET_VALUES, 6, msg_preset, (int)sizeof(msg_preset));

    if (msg_dummy_len <= 0 || msg_preset_len <= 0) {
        std::cerr << "[NEG] ERROR: Failed to build negotiation packets." << std::endl;
        return false;
    }

    // send_udp_packet(sd, target, msg_dummy, msg_dummy_len, "dummy message");
    // send_udp_packet(sd, target, msg_preset, msg_preset_len, "preset float message");

    auto t0 = std::chrono::steady_clock::now();
    auto last_resend = t0;
    bool send_dummy_next = true;

    std::cout << "[NEG] Waiting for READY_ from " << EXPECTED_READY_IP << ":" << EXPECTED_READY_PORT << "..." << std::endl;

    while (true) {
        auto now = std::chrono::steady_clock::now();
        int elapsed_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count();
        if (elapsed_ms > NEGOTIATION_TIMEOUT_MS) {
            std::cerr << "[NEG] ERROR: Timeout waiting READY_." << std::endl;
            return false;
        }

        /* int resend_elapsed_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - last_resend).count();
        if (resend_elapsed_ms >= RESEND_PERIOD_MS) {
            if (send_dummy_next) {
                send_udp_packet(sd, target, msg_dummy, msg_dummy_len, "dummy message");
            } else {
                send_udp_packet(sd, target, msg_preset, msg_preset_len, "preset float message");
            }
            send_dummy_next = !send_dummy_next;
            last_resend = now;
        } */

        if (_kbhit())
        {
            char c = (char)_getch();

            if (c == 'q')
            {
                printf("Exiting server...\n");
                break;
            }else if (c == 'd')
            {
                send_udp_packet(sd, target, msg_dummy, msg_dummy_len, "dummy message");
            }else if (c == 'p')
            {
                send_udp_packet(sd, target, msg_preset, msg_preset_len, "preset float message");
            }
        }    

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sd, &readfds);

        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;

        int sel = select(0, &readfds, NULL, NULL, &tv);
        if (sel == SOCKET_ERROR) {
            std::cerr << "[UDP] ERROR: select failed, WSA=" << WSAGetLastError() << std::endl;
            return false;
        }

        if (sel == 0) {
            continue;
        }

        unsigned char rx[1024];
        sockaddr_in from{};
        int from_len = sizeof(from);

        int n = recvfrom(sd, (char*)rx, (int)sizeof(rx), 0, (sockaddr*)&from, &from_len);
        if (n == SOCKET_ERROR) {
            std::cerr << "[UDP] ERROR: recvfrom failed, WSA=" << WSAGetLastError() << std::endl;
            continue;
        }

        std::cout << "[UDP] Received packet from " << inet_ntoa(from.sin_addr)
                  << ":" << ntohs(from.sin_port) << std::endl;
        print_bytes(rx, n);

        char text[128];
        int text_len = decode_text_from_float_payload(rx, n, text, (int)sizeof(text));
        if (text_len > 0) {
            std::cout << "[UDP] Decoded text: " << text << std::endl;

            if (std::strcmp(text, READY_TEXT) == 0) {
                // printf("[NEG] READY_ received from port %d (expected %d)\n", ntohs(from.sin_port), EXPECTED_READY_PORT);
                if (is_expected_ready_sender(from)) {
                    std::cout << "[NEG] READY_ validated from expected endpoint." << std::endl;
                    return true;
                }
                std::cout << "[NEG] READY_ received but endpoint does not match expected sender." << std::endl;
                return true;
            }
        }
    }
}

/************************************************************************/
// Main function

int main() {
    REAL_TIME_MODE realTimeMode = SoftSimMode; // software synchronization mode (can be changed to SIM_MODE or HARD_SYNC_MODE depending on needs and model configuration)
    int ret = -1; // use -1 as default error code; Opal API returns 0 on success
    WSADATA wsaData{};
    SOCKET udp_sd = INVALID_SOCKET;
    bool winsock_ok = false;
    bool project_open = false;
    double timeFactor = 1.0; // time factor for OpalLoad and OpalExecute; only relevant if realTimeMode is 1 or 2
    OP_API_INSTANCE_ID instanceId = 0; // instance id required by newer OpalApi signatures

    std::cout << "[OPAL] Opening project: " << PROJECT_PATH << std::endl;
    ret = OpalOpenProject(PROJECT_PATH, 0, &instanceId);
    if (ret != 0) {
        PrintError(ret, "OpalOpenProject");
		return(ret);
    }
    project_open = true;

    std::cout << "[OPAL] Pre-loading model binaries..." << std::endl;
    ret = OpalLoad((unsigned short)realTimeMode, &instanceId, timeFactor);
    if (ret != 0) {
        PrintError(ret, "OpalLoad");
        OpalCloseProject();
        return 1;
    }
    std::cout << "[OPAL] Model pre-loaded and paused." << std::endl;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[UDP] ERROR: WSAStartup failed." << std::endl;
        OpalCloseProject();
        return 1;
    }
    winsock_ok = true;

    udp_sd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_sd == INVALID_SOCKET) {
        std::cerr << "[UDP] ERROR: Could not create UDP socket." << std::endl;
        WSACleanup();
        OpalCloseProject();
        return 1;
    }

    sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons((u_short)LOCAL_LISTEN_PORT);
    local_addr.sin_addr.s_addr = inet_addr(LOCAL_LISTEN_IP);
    if (local_addr.sin_addr.s_addr == INADDR_NONE) {
        std::cerr << "[UDP] ERROR: Invalid LOCAL_LISTEN_IP." << std::endl;
        closesocket(udp_sd);
        WSACleanup();
        OpalCloseProject();
        return 1;
    }

    if (bind(udp_sd, (sockaddr*)&local_addr, sizeof(local_addr)) == SOCKET_ERROR) {
        std::cerr << "[UDP] ERROR: bind failed on " << LOCAL_LISTEN_IP << ":" << LOCAL_LISTEN_PORT
                  << " (WSA=" << WSAGetLastError() << ")" << std::endl;
        std::cerr << "[UDP] Hint: use 0.0.0.0 if this machine has multiple NICs or IP mismatch." << std::endl;
        closesocket(udp_sd);
        WSACleanup();
        OpalCloseProject();
        return 1;
    }

    sockaddr_in target_addr{};
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons((u_short)NEGOTIATION_TARGET_PORT);
    target_addr.sin_addr.s_addr = inet_addr(NEGOTIATION_TARGET_IP);
    if (target_addr.sin_addr.s_addr == INADDR_NONE) {
        std::cerr << "[UDP] ERROR: Invalid NEGOTIATION_TARGET_IP." << std::endl;
        closesocket(udp_sd);
        WSACleanup();
        OpalCloseProject();
        return 1;
    }

    std::cout << "[UDP] Local endpoint: " << LOCAL_LISTEN_IP << ":" << LOCAL_LISTEN_PORT << std::endl;
    std::cout << "[UDP] Negotiation target: " << NEGOTIATION_TARGET_IP << ":" << NEGOTIATION_TARGET_PORT << std::endl;

    bool got_ready = wait_for_ready_and_negotiate(udp_sd, target_addr);
    if (!got_ready) {
        std::cerr << "[OPAL] Execution canceled because READY_ was not validated." << std::endl;
        closesocket(udp_sd);
        WSACleanup();
        OpalCloseProject();
        return 1;
    }

    auto exec_start = std::chrono::high_resolution_clock::now();
    ret = OpalExecute(timeFactor);
    auto exec_end = std::chrono::high_resolution_clock::now();

    long long trigger_ms = std::chrono::duration_cast<std::chrono::milliseconds>(exec_end - exec_start).count();
    if (ret == 0) {
        std::cout << "[OPAL] Execute OK. Trigger latency=" << trigger_ms << " ms" << std::endl;
    } else {
        std::cerr << "[OPAL] ERROR: OpalExecute failed. Code=" << ret << std::endl;
    }

        // Waits for 30 seconds before resetting and closing the project, to allow time for the model to run and for the user to see the output
#if defined(WIN32)
	Sleep(30000);
#else
	delay(30000);
#endif

    if (udp_sd != INVALID_SOCKET) {
        closesocket(udp_sd);
    }
    
    if (winsock_ok) {
        WSACleanup();
    }



    // Reset the model
	ret = OpalReset();
	if(EOK != ret)
	{
		PrintError(ret, "OpalReset");
		return(ret);
	}
	printf("- Model is reset.\n");

    // Release the control of the system
    ret = OpalGetSystemControl(0);
	if(EOK != ret)
	{
		PrintError(ret, "OpalGetSystemControl");
		return(ret);
	}
	
	// Disconnect from the model
	OpalDisconnect();
	printf("- Model is disconnected.\n");

	system("pause");

    return (ret == 0) ? 0 : 1;
}
