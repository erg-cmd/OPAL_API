/* send_ready.cpp
 * Small utility to send a READY_ payload encoded as floats (little-endian text payload)
 * Usage: send_ready.exe <target_ip> <target_port>
 * Defaults: 127.0.0.1 5008
 */

#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdint.h>

#pragma comment(lib, "ws2_32.lib")

static void pack_float_little_endian(float value, unsigned char out[4])
{
    union { float f; uint32_t u; } cvt;
    cvt.f = value;
    out[0] = (unsigned char)(cvt.u & 0xFF);
    out[1] = (unsigned char)((cvt.u >> 8) & 0xFF);
    out[2] = (unsigned char)((cvt.u >> 16) & 0xFF);
    out[3] = (unsigned char)((cvt.u >> 24) & 0xFF);
}

static int build_message_from_text_little_endian(const char *text, unsigned char *out_msg, int out_cap)
{
    if (!text || !out_msg) return -1;
    int text_len = (int)strlen(text);
    int payload_len = text_len * 4;
    int total_len = payload_len + 1;
    if (payload_len > 255 || total_len > out_cap) return -1;
    out_msg[0] = (unsigned char)payload_len;
    for (int i = 0; i < text_len; ++i)
        pack_float_little_endian((float)(unsigned char)text[i], &out_msg[1 + (i * 4)]);
    return total_len;
}

int main(int argc, char **argv)
{
    const char *target_ip = "127.0.0.1";
    int target_port = 5008;
    if (argc > 1) target_ip = argv[1];
    if (argc > 2) target_port = atoi(argv[2]);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    SOCKET sd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sd == INVALID_SOCKET) {
        fprintf(stderr, "socket() failed\n");
        WSACleanup();
        return 1;
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons((u_short)target_port);
    dest.sin_addr.s_addr = inet_addr(target_ip);

    unsigned char buf[255];
    int len = build_message_from_text_little_endian("READY_", buf, (int)sizeof(buf));
    if (len <= 0) {
        fprintf(stderr, "Failed to build READY_ payload\n");
        closesocket(sd);
        WSACleanup();
        return 1;
    }

    int rc = sendto(sd, (const char*)buf, len, 0, (struct sockaddr*)&dest, sizeof(dest));
    if (rc == SOCKET_ERROR) {
        fprintf(stderr, "sendto() failed: %d\n", WSAGetLastError());
    } else {
        printf("Sent READY_ (%d bytes) to %s:%d\n", rc, target_ip, target_port);
    }

    closesocket(sd);
    WSACleanup();
    return 0;
}
