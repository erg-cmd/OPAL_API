/**----------------------------------------
 * @file devil_trigger_v3.cpp
 * @author Elias R. Gracia (elias.gracia@uah.es)
 * @brief Python-equivalent OPAL preload, pause check, and READY negotiation.
 * This code is a revised version of the UDP-triggered OPAL-RT execution, 
 * aiming to improve the negotiation process. This Code is a reinterpretation of
 * "triggerv2.py".
 * @version 0.1
 * @date 2026-06-04
 * @state Not working, for reference only
 * @copyright Copyright (c) 2026
 * 
 * Compile with:
 * g++ devil_trigger_v3.cpp -o devil_trigger_v3.exe -I"C:\OPAL-RT\RT-LAB\2021.3.4\common\include" -L"C:\OPAL-RT\RT-LAB\2021.3.4\common\lib" -lOpalApi -lws2_32
 *
 */

#include <chrono>
#include <cstdint>
#include <conio.h>
#include <cstring>
#include <iostream>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "C:/OPAL-RT/RT-LAB/2021.3.4/common/include/OpalApi.h"

#pragma comment(lib, "ws2_32.lib")

using namespace std::chrono;

static const char *PROJECT_PATH = "C:\\Archivos_INI_OPAL\\ergs_test5\\ergs_test5.llp";
static const char *UDP_IP = "0.0.0.0";
static const int UDP_PORT = 5008;
static const char *NEGOTIATION_TARGET_IP = "192.168.10.123";
static const int NEGOTIATION_TARGET_PORT = 5008;
static const char *EXPECTED_READY_IP = "192.168.10.123";
static const int EXPECTED_READY_PORT = 5008;
static const char *READY_TEXT = "READY_";
static const char *DUMMY_TEXT = "ByeBye";
static const float PRESET_VALUES[6] = {1.0f, 0.005f, 30.0f, 1.0f, 2.0f, 10.0f};
static const double NEGOTIATION_TIMEOUT_SECONDS = 60.0;
static const double POLL_PERIOD_SECONDS = 1.5;

static void print_error(int rc, const char *funcName)
{
    char buf[512];
    unsigned short len = 0;
    OpalGetLastErrMsg(buf, sizeof(buf), &len);
    std::cerr << "Error " << buf << " (code " << rc << ") from " << funcName << std::endl;
}

static std::string describe_model_state(short state)
{
    switch (state)
    {
    case MODEL_NOT_CONNECTED: return "MODEL_NOT_CONNECTED";
    case MODEL_NOT_LOADABLE: return "MODEL_NOT_LOADABLE";
    case MODEL_COMPILING: return "MODEL_COMPILING";
    case MODEL_LOADABLE: return "MODEL_LOADABLE";
    case MODEL_LOADING: return "MODEL_LOADING";
    case MODEL_RESETTING: return "MODEL_RESETTING";
    case MODEL_LOADED: return "MODEL_LOADED";
    case MODEL_PAUSED: return "MODEL_PAUSED";
    case MODEL_RUNNING: return "MODEL_RUNNING";
    case MODEL_DISCONNECTED: return "MODEL_DISCONNECTED";
    default: return "UNKNOWN_STATE(" + std::to_string((int)state) + ")";
    }
}

static void pack_float_little_endian(float value, unsigned char out[4])
{
    union
    {
        float f;
        uint32_t u;
    } cvt;

    cvt.f = value;
    out[0] = (unsigned char)(cvt.u & 0xFFU);
    out[1] = (unsigned char)((cvt.u >> 8) & 0xFFU);
    out[2] = (unsigned char)((cvt.u >> 16) & 0xFFU);
    out[3] = (unsigned char)((cvt.u >> 24) & 0xFFU);
}

static double unpack_little_endian_to_float(const unsigned char in[4])
{
    uint32_t u = ((uint32_t)in[0]) |
                 ((uint32_t)in[1] << 8) |
                 ((uint32_t)in[2] << 16) |
                 ((uint32_t)in[3] << 24);

    union
    {
        uint32_t u;
        float f;
    } cvt;

    cvt.u = u;
    return (double)cvt.f;
}

static int build_message_from_text_little_endian(const char *text, unsigned char *out_msg, int out_cap)
{
    if (text == NULL || out_msg == NULL)
    {
        return -1;
    }

    int text_len = (int)std::strlen(text);
    int payload_len = text_len * 4;
    int total_len = payload_len + 1;

    if (payload_len > 255 || total_len > out_cap)
    {
        return -1;
    }

    out_msg[0] = (unsigned char)payload_len;
    for (int i = 0; i < text_len; ++i)
    {
        pack_float_little_endian((float)(unsigned char)text[i], &out_msg[1 + (i * 4)]);
    }

    return total_len;
}

static int build_message_from_float_array(const float *values, int count, unsigned char *out_msg, int out_cap)
{
    if (values == NULL || out_msg == NULL)
    {
        return -1;
    }

    int payload_len = count * 4;
    int total_len = payload_len + 1;

    if (payload_len > 255 || total_len > out_cap)
    {
        return -1;
    }

    out_msg[0] = (unsigned char)payload_len;
    for (int i = 0; i < count; ++i)
    {
        pack_float_little_endian(values[i], &out_msg[1 + (i * 4)]);
    }

    return total_len;
}

static int decode_text_from_float_payload(const unsigned char *msg, int msg_len, char *out_text, int out_cap)
{
    if (msg == NULL || out_text == NULL || msg_len < 1 || out_cap < 1)
    {
        return -1;
    }

    int payload_len = (int)msg[0];
    if (msg_len < 1 + payload_len || (payload_len % 4) != 0)
    {
        return -1;
    }

    int text_len = payload_len / 4;
    if (text_len + 1 > out_cap)
    {
        return -1;
    }

    for (int i = 0; i < text_len; ++i)
    {
        double v = unpack_little_endian_to_float(&msg[1 + (i * 4)]);
        if (v < 0.0 || v > 255.0)
        {
            return -1;
        }

        out_text[i] = (char)((unsigned char)((int)v));
    }

    out_text[text_len] = '\0';
    return text_len;
}

static bool is_expected_ready_sender(const sockaddr_in &from)
{
    if (ntohs(from.sin_port) != EXPECTED_READY_PORT)
    {
        return false;
    }

    unsigned long expected_ip = inet_addr(EXPECTED_READY_IP);
    if (expected_ip == INADDR_NONE)
    {
        return false;
    }

    return from.sin_addr.s_addr == expected_ip;
}

static bool wait_for_pause_state()
{
    const auto deadline = steady_clock::now() + duration_cast<steady_clock::duration>(duration<double>(NEGOTIATION_TIMEOUT_SECONDS));

    std::cout << " -> Monitoring target node initialization progress..." << std::endl;
    while (steady_clock::now() < deadline)
    {
        short modelState = MODEL_NOT_CONNECTED;
        unsigned short realTimeMode = 0;
        int ret = OpalGetModelState(&modelState, &realTimeMode);
        if (ret != EOK)
        {
            print_error(ret, "OpalGetModelState");
            Sleep((DWORD)(POLL_PERIOD_SECONDS * 1000.0));
            continue;
        }

        std::cout << "   [Target Status]: " << describe_model_state(modelState) << std::endl;
        if (modelState == MODEL_PAUSED)
        {
            return true;
        }

        Sleep((DWORD)(POLL_PERIOD_SECONDS * 1000.0));
    }

    return false;
}

static bool check_for_pause_state()
{
    short modelState = MODEL_NOT_CONNECTED;
    unsigned short realTimeMode = 0;
    int ret = OpalGetModelState(&modelState, &realTimeMode);
    if (ret != EOK)
    {
        print_error(ret, "OpalGetModelState");
        return false;
    }

    std::cout << "[OPAL] Current model state: " << describe_model_state(modelState) << std::endl;

    if (modelState == MODEL_RUNNING)
    {
        std::cout << "[OPAL] Model is currently RUNNING. Attempting to PAUSE..." << std::endl;
        ret = OpalPause();
        if (ret != EOK)
        {
            print_error(ret, "OpalPause");
            return false;
        }

        Sleep(2000);
        return true;
    }

    if (modelState == MODEL_PAUSED)
    {
        std::cout << "[OPAL] Model is already PAUSED." << std::endl;
        return true;
    }

    std::cout << "[OPAL] Model is in an unexpected state. Expected RUNNING or PAUSED." << std::endl;
    return false;
}

static bool send_udp_packet(SOCKET sd, const sockaddr_in &target, const unsigned char *msg, int len, const char *label)
{
    int sent = sendto(sd, (const char *)msg, len, 0, (const sockaddr *)&target, sizeof(target));
    if (sent == SOCKET_ERROR)
    {
        std::cerr << "[UDP] ERROR: sendto failed for " << label << ", WSA=" << WSAGetLastError() << std::endl;
        return false;
    }

    std::cout << "[UDP] Sent " << label << " (" << sent << " bytes) to "
              << NEGOTIATION_TARGET_IP << ":" << NEGOTIATION_TARGET_PORT << std::endl;
    return true;
}

static bool wait_for_ready_and_negotiate(SOCKET sd, const sockaddr_in &target)
{
    unsigned char msg_dummy[256];
    unsigned char msg_preset[256];

    int msg_dummy_len = build_message_from_text_little_endian(DUMMY_TEXT, msg_dummy, (int)sizeof(msg_dummy));
    int msg_preset_len = build_message_from_float_array(PRESET_VALUES, 6, msg_preset, (int)sizeof(msg_preset));

    if (msg_dummy_len <= 0 || msg_preset_len <= 0)
    {
        std::cerr << "[NEG] ERROR: Failed to build negotiation packets." << std::endl;
        return false;
    }

    std::cout << "[NEG] Waiting for READY_ from " << EXPECTED_READY_IP << ":" << EXPECTED_READY_PORT << "..." << std::endl;
    std::cout << "[NEG] Press 'd' to send dummy message, 'p' to send preset float message, 'q' to quit." << std::endl;

    const auto start_time = steady_clock::now();

    while (true)
    {
        if (duration<double>(steady_clock::now() - start_time).count() > NEGOTIATION_TIMEOUT_SECONDS)
        {
            std::cerr << "[NEG] ERROR: Timeout waiting READY_." << std::endl;
            return false;
        }

        if (_kbhit())
        {
            char c = (char)_getch();
            if (c == 'q')
            {
                std::cout << "Exiting server..." << std::endl;
                return false;
            }
            if (c == 'd')
            {
                send_udp_packet(sd, target, msg_dummy, msg_dummy_len, "dummy message");
            }
            else if (c == 'p')
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
        if (sel == SOCKET_ERROR)
        {
            std::cerr << "[UDP] ERROR: select failed, WSA=" << WSAGetLastError() << std::endl;
            return false;
        }

        if (sel == 0)
        {
            continue;
        }

        unsigned char rx[1024];
        sockaddr_in from{};
        int from_len = sizeof(from);

        int n = recvfrom(sd, (char *)rx, (int)sizeof(rx), 0, (sockaddr *)&from, &from_len);
        if (n == SOCKET_ERROR)
        {
            std::cerr << "[UDP] ERROR: recvfrom failed, WSA=" << WSAGetLastError() << std::endl;
            continue;
        }

        std::cout << "[UDP] Received packet from " << inet_ntoa(from.sin_addr)
                  << ":" << ntohs(from.sin_port) << std::endl;
        std::cout << "[UDP] Bytes (" << n << "): ";
        for (int i = 0; i < n; ++i)
        {
            std::cout << (unsigned int)rx[i] << " ";
        }
        std::cout << std::endl;

        char decoded[128];
        int text_len = decode_text_from_float_payload(rx, n, decoded, (int)sizeof(decoded));
        if (text_len <= 0)
        {
            continue;
        }

        std::cout << "[UDP] Decoded text: " << decoded << std::endl;
        if (std::strcmp(decoded, READY_TEXT) == 0)
        {
            if (is_expected_ready_sender(from))
            {
                std::cout << "[NEG] READY_ validated from expected endpoint." << std::endl;
                return true;
            }

            std::cout << "[NEG] READY_ received but endpoint does not match expected sender." << std::endl;
            return false;
        }
    }
}

int main()
{
    int ret = -1;
    OP_API_INSTANCE_ID instanceId = 0;
    WSADATA wsaData{};
    SOCKET sock = INVALID_SOCKET;
    bool winsock_ok = false;

    std::cout << "[OPAL] Opening project: " << PROJECT_PATH << std::endl;
    ret = OpalOpenProject(PROJECT_PATH, 0, &instanceId);
    if (ret != EOK)
    {
        print_error(ret, "OpalOpenProject");
        return 1;
    }

    std::cout << "[OPAL] Acquiring system control..." << std::endl;
    ret = OpalGetSystemControl(1);
    if (ret != EOK)
    {
        print_error(ret, "OpalGetSystemControl");
        OpalCloseProject();
        return 1;
    }
    std::cout << "[OPAL] System control requested." << std::endl;

    std::cout << "[OPAL] Pre-loading model binaries..." << std::endl;
    ret = OpalLoad(2, &instanceId, 1.0);
    if (ret != EOK)
    {
        print_error(ret, "OpalLoad");
        OpalGetSystemControl(0);
        OpalCloseProject();
        return 1;
    }

    short modelState = MODEL_NOT_CONNECTED;
    unsigned short realTimeMode = 0;
    ret = OpalGetModelState(&modelState, &realTimeMode);
    if (ret == EOK)
    {
        std::cout << "[OPAL] Load response: " << describe_model_state(modelState) << std::endl;
    }

    std::cout << "[OPAL] Acquiring monitoring control..." << std::endl;
    ret = OpalGetMonitoringControl(1);
    if (ret != EOK)
    {
        print_error(ret, "OpalGetMonitoringControl");
        OpalGetSystemControl(0);
        OpalCloseProject();
        return 1;
    }
    std::cout << "[OPAL] Monitoring control requested." << std::endl;

    if (!check_for_pause_state())
    {
        std::cout << "[OPAL] Model state was unexpected, requesting pause..." << std::endl;
        ret = OpalPause();
        if (ret != EOK)
        {
            print_error(ret, "OpalPause");
            OpalGetMonitoringControl(0);
            OpalGetSystemControl(0);
            OpalCloseProject();
            return 1;
        }
    }

    if (!wait_for_pause_state())
    {
        std::cerr << "\n[ERROR] Target failed to enter a stable PAUSE state within 60s. Aborting." << std::endl;
        OpalGetMonitoringControl(0);
        OpalGetSystemControl(0);
        OpalCloseProject();
        return 1;
    }

    std::cout << "\n[SUCCESS] Target perfectly mirrors the GUI state: PAUSED." << std::endl;
    std::cout << "[STATUS] AsyncIP network drivers are running on target. Awaiting UDP trigger...\n" << std::endl;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "[UDP] ERROR: WSAStartup failed." << std::endl;
        OpalGetMonitoringControl(0);
        OpalGetSystemControl(0);
        OpalCloseProject();
        return 1;
    }
    winsock_ok = true;

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
    {
        std::cerr << "[UDP] ERROR: Could not create UDP socket." << std::endl;
        WSACleanup();
        OpalGetMonitoringControl(0);
        OpalGetSystemControl(0);
        OpalCloseProject();
        return 1;
    }

    sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons((u_short)UDP_PORT);
    local_addr.sin_addr.s_addr = inet_addr(UDP_IP);
    if (local_addr.sin_addr.s_addr == INADDR_NONE)
    {
        local_addr.sin_addr.s_addr = INADDR_ANY;
    }

    if (bind(sock, (sockaddr *)&local_addr, sizeof(local_addr)) == SOCKET_ERROR)
    {
        std::cerr << "[UDP] ERROR: bind failed on " << UDP_IP << ":" << UDP_PORT
                  << " (WSA=" << WSAGetLastError() << ")" << std::endl;
        closesocket(sock);
        WSACleanup();
        OpalGetMonitoringControl(0);
        OpalGetSystemControl(0);
        OpalCloseProject();
        return 1;
    }

    sockaddr_in target_addr{};
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons((u_short)NEGOTIATION_TARGET_PORT);
    target_addr.sin_addr.s_addr = inet_addr(NEGOTIATION_TARGET_IP);
    if (target_addr.sin_addr.s_addr == INADDR_NONE)
    {
        std::cerr << "[UDP] ERROR: Invalid NEGOTIATION_TARGET_IP." << std::endl;
        closesocket(sock);
        WSACleanup();
        OpalGetMonitoringControl(0);
        OpalGetSystemControl(0);
        OpalCloseProject();
        return 1;
    }

    std::cout << "[UDP] Listening on port " << UDP_PORT << " for negotiation and trigger messages..." << std::endl;

    bool got_ready = wait_for_ready_and_negotiate(sock, target_addr);
    if (!got_ready)
    {
        std::cerr << "[OPAL] Execution canceled because READY_ was not validated." << std::endl;
        closesocket(sock);
        WSACleanup();
        OpalGetMonitoringControl(0);
        OpalGetSystemControl(0);
        OpalCloseProject();
        return 1;
    }

    std::cout << "[OPAL] READY_ validated. Starting execution..." << std::endl;
    const auto exec_start = steady_clock::now();
    ret = OpalExecute(1.0);
    const auto exec_end = steady_clock::now();
    const auto latency_ms = duration_cast<milliseconds>(exec_end - exec_start).count();

    if (ret == EOK)
    {
        std::cout << "[OPAL] Execute OK. Trigger latency=" << latency_ms << " ms" << std::endl;
    }
    else
    {
        print_error(ret, "OpalExecute");
    }

    Sleep(31000);

    if (sock != INVALID_SOCKET)
    {
        closesocket(sock);
    }
    if (winsock_ok)
    {
        WSACleanup();
    }

    ret = OpalGetMonitoringControl(0);
    if (ret != EOK)
    {
        print_error(ret, "OpalGetMonitoringControl");
        OpalGetSystemControl(0);
        OpalCloseProject();
        return 1;
    }

    ret = OpalGetSystemControl(0);
    if (ret != EOK)
    {
        print_error(ret, "OpalGetSystemControl");
        OpalCloseProject();
        return 1;
    }

    ret = OpalCloseProject();
    if (ret != EOK)
    {
        print_error(ret, "OpalCloseProject");
        return 1;
    }

    std::cout << "[OPAL] Project closed and API disconnected cleanly." << std::endl;
    return 0;
}

