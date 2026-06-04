/**----------------------------------------
 * @file Server_simulator.c
 * @author your name (you@domain.com)
 * @brief sends a dummy or a predefined message, if the lient response with "READY_"
 * then receives messages and answers back a mesasge of six floats until no more messages are
 * received in 0.5 seconds, then it goes back to sending messages.
 * @version 0.1
 * @date 2026-04-20
 * NOTE: compile with "gcc Server_simulator.c -o Server_simulator.exe -lwsock32 -ladvapi32 -lwinmm"
 * @copyright Copyright (c) 2026
 *
 ----------------------------------------*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <winsock2.h>
#include <time.h>

/* Visual Studio pragma links (ignored by MinGW) */
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "wsock32.lib")
#pragma comment(lib, "advapi32.lib")

/*-----------------------------------------------------------------
 * Displays received packets and sends them back to the originator.
 *-----------------------------------------------------------------*/

#define SERVER_IP_ADDRESS "192.168.10.56" // LOCAL IP ADDRESS
#define SERVER_PORT 5008

#define REPLY_IP_ADDRESS "192.168.10.56"
#define REPLY_PORT 5009

#define ENABLE_RESEND 0

/**----------------------------------------
 * @brief
 *
 * @param value
 * @param out
 ----------------------------------------*/
static void pack_float_little_endian(float value, unsigned char out[4])
{
    union
    {
        float f;
        uint32_t u;
    } cvt;

    cvt.f = value;
    out[0] = (unsigned char)(cvt.u & 0xFF);
    out[1] = (unsigned char)((cvt.u >> 8) & 0xFF);
    out[2] = (unsigned char)((cvt.u >> 16) & 0xFF);
    out[3] = (unsigned char)((cvt.u >> 24) & 0xFF);
}

static void pack_float_big_endian(float value, unsigned char out[4])
{
    union
    {
        float f;
        uint32_t u;
    } cvt;

    cvt.f = value;
    out[3] = (unsigned char)(cvt.u & 0xFF);
    out[2] = (unsigned char)((cvt.u >> 8) & 0xFF);
    out[1] = (unsigned char)((cvt.u >> 16) & 0xFF);
    out[0] = (unsigned char)((cvt.u >> 24) & 0xFF);
}

/**
 * @brief Unpacks the char array and presents the float numbers
 */
static double unpack_little_endian_to_float(const unsigned char in[4])
{
    uint32_t u = ((uint32_t)in[0]) | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
    union
    {
        uint32_t u;
        float f;
    } cvt;
    cvt.u = u;
    return (double) cvt.f;
}

/**----------------------------------------
 * @brief
 *
 * @param text
 * @param out_msg
 * @param out_cap
 * @return int
 ----------------------------------------*/
static int build_message_from_text_little_endian(const char *text, unsigned char *out_msg, int out_cap)
{
    int text_len;
    int payload_len;
    int total_len;
    int i;

    if (text == NULL || out_msg == NULL)
        return -1;

    text_len = (int)strlen(text);
    payload_len = text_len * 4;
    total_len = payload_len + 1;

    if (payload_len > 255 || total_len > out_cap)
        return -1;

    out_msg[0] = (unsigned char)payload_len;
    for (i = 0; i < text_len; i++)
        pack_float_little_endian((float)(unsigned char)text[i], &out_msg[1 + (i * 4)]);

    return total_len;
}

static int build_message_from_text_big_endian(const char *text, unsigned char *out_msg, int out_cap)
{
    int text_len;
    int payload_len;
    int total_len;
    int i;

    if (text == NULL || out_msg == NULL)
        return -1;

    text_len = (int)strlen(text);
    payload_len = text_len * 4;
    total_len = payload_len + 1;

    if (payload_len > 255 || total_len > out_cap)
        return -1;

    out_msg[0] = (unsigned char)payload_len;
    for (i = 0; i < text_len; i++)
        pack_float_big_endian((float)(unsigned char)text[i], &out_msg[1 + (i * 4)]);

    return total_len;
}

static int build_message_from_float_array(const float *values, int count, unsigned char *out_msg, int out_cap)
{
    int payload_len = count * 4;
    int total_len = payload_len + 1;

    if (values == NULL || out_msg == NULL || payload_len > 255 || total_len > out_cap)
        return -1;

     out_msg[0] = (unsigned char)payload_len;
     for (int i = 0; i < count; i++)
         pack_float_little_endian(values[i], &out_msg[1 + (i * 4)]);

     return total_len;
}

static int decode_text_from_float_payload(const unsigned char *msg, int msg_len, char *out_text, int out_cap)
{
    int payload_len;
    int text_len;
    int i;

    if (msg == NULL || out_text == NULL || msg_len < 1 || out_cap < 1)
        return -1;

    payload_len = (int)msg[0];
    if (msg_len < 1 + payload_len || (payload_len % 4) != 0)
        return -1;

    text_len = payload_len / 4;
    if (text_len + 1 > out_cap)
        return -1;

    for (i = 0; i < text_len; i++)
    {
        double v = unpack_little_endian_to_float(&msg[1 + (i * 4)]);
        if (v < 0.0 || v > 255.0)
            return -1;
        out_text[i] = (char)((unsigned char)((int)v));
    }

    out_text[text_len] = '\0';
    return text_len;
}

static int socket_wait_readable(SOCKET sd_wait, int timeout_ms)
{
    fd_set read_fds;
    struct timeval tv;

    FD_ZERO(&read_fds);
    FD_SET(sd_wait, &read_fds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    return select(0, &read_fds, NULL, NULL, &tv);
}

static int send_packet(SOCKET sd_send, int tcp_proto, const unsigned char *msg, int msg_len, const struct sockaddr_in *client_ad_send)
{
    if (tcp_proto)
        return send(sd_send, (const char *)msg, msg_len, 0);

    if (client_ad_send == NULL)
        return SOCKET_ERROR;

    return sendto(sd_send, (const char *)msg, msg_len, 0, (const struct sockaddr *)client_ad_send, sizeof(*client_ad_send));
}

static int receive_packet(SOCKET sd_recv, int tcp_proto, unsigned char *data, int data_cap, struct sockaddr_in *client_ad_recv, int *client_len_recv)
{
    if (tcp_proto)
        return recv(sd_recv, (char *)data, data_cap, 0);

    if (client_ad_recv == NULL || client_len_recv == NULL)
        return SOCKET_ERROR;

    return recvfrom(sd_recv, (char *)data, data_cap, 0, (struct sockaddr *)client_ad_recv, client_len_recv);
}

static int set_socket_nonblocking(SOCKET sd_set)
{
    u_long ioctlVal = 1;
    return ioctlsocket(sd_set, FIONBIO, &ioctlVal);
}

static int create_bound_udp_socket(const char *ip_address, int port, SOCKET *out_sd, struct sockaddr_in *out_ad)
{
    SOCKET new_sd;

    if (out_sd == NULL || out_ad == NULL)
        return -1;

    new_sd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (new_sd == INVALID_SOCKET)
        return -1;

    memset((char *)out_ad, 0, sizeof(*out_ad));
    out_ad->sin_family = AF_INET;
    if (strcmp(ip_address, "0.0.0.0") == 0)
        out_ad->sin_addr.s_addr = INADDR_ANY;
    else
        out_ad->sin_addr.s_addr = inet_addr(ip_address);
    out_ad->sin_port = htons((u_short)port);

    if (bind(new_sd, (struct sockaddr *)out_ad, sizeof(*out_ad)) == SOCKET_ERROR)
    {
        closesocket(new_sd);
        return -1;
    }

    if (set_socket_nonblocking(new_sd) == SOCKET_ERROR)
    {
        closesocket(new_sd);
        return -1;
    }

    *out_sd = new_sd;
    return 0;
}

/**----------------------------------------
 * @brief This function returns the current time in milliseconds since the program started, using the timeGetTime function from the Windows API.
 * 
 * @return float 
 */
float current_millis(void)
{
    return (float)timeGetTime();
}

static void update_msg2_values(float values[6], float program_start_time)
{
    float elapsed_ms;
    float torque_inc = 0;
    elapsed_ms = current_millis() - program_start_time;
    // torque_inc = -70000 - 7 * (elapsed_ms / 5);  // works with 5MW
    // torque_inc = -100000 - 2 * (elapsed_ms / 10); // works with 5MW
    torque_inc = 500000; 
    values[0] = elapsed_ms;
    values[1] = 0.5f;
    values[2] = 0.5f;
    values[3] = 0.5f;
    values[4] = torque_inc;
    values[5] = 0.0f;
}

static int log_received_packet(const unsigned char *data, int n, double *aux_ff)
{
    int i;
    int ready_received = 0;
    char received_text[64];

    printf("Received (%d bytes): ", n);
    for (i = 0; i < n; i++)
        printf("%u ", (unsigned char)data[i]);
    printf("\n");

    if (n < 1)
        return 0;

    printf("Pre-decode payload_len=%u\n", (unsigned char)data[0]);

    {
        unsigned char payload_len = (unsigned char)data[0];

        if (n < 1 + payload_len)
        {
            printf("WARNING: incomplete payload (expected %u payload bytes, got %d)\n", payload_len, n - 1);
        }
        else if (payload_len == 0)
        {
            printf("Received empty payload\n");
        }
        else if (payload_len % 4 != 0)
        {
            printf("WARNING: payload length %u is not multiple of 4 (cannot be whole floats)\n", payload_len);
        }
        else
        {
            int num_floats = payload_len / 4;
            printf("Post-decode floats (LE): [");
            for (i = 0; i < num_floats; i++)
            {
                int off = 1 + i * 4;
                if (off + 3 >= n)
                {
                    printf(" <incomplete>");
                    break;
                }
                *aux_ff = unpack_little_endian_to_float((unsigned char *)&data[off]);
                if (i)
                    printf(", ");
                printf("%f", *aux_ff);
            }
            printf("]\n");

            if (decode_text_from_float_payload(data, n, received_text, (int)sizeof(received_text)) > 0)
            {
                printf("Post-decode text: %s\n", received_text);
                if (strcmp(received_text, "READY_") == 0)
                    ready_received = 1;
            }
        }
    }

    return ready_received;
}



/**----------------------------------------
 * @brief sends a dummy or a predefined message, if the lient response with "READY_"
 * then receives messages and answers back a mesasge of six floats until no more messages are
 * received in 0.5 seconds, then it goes back to sending messages.
 *
 * @return int
 ----------------------------------------*/
int main(void)
{
    const char msg1_str[] = "ByeBye";
    // const float msg2_str[] = {1.0f, 0.005f, 30.0f, 1.0f, 2.0f, 10.0f};
    const float msg2_str[] = {2.0f, 0.005f, 30.0f, 1.0f, 2.0f, 10.0f};
    const char msg3_str[] = "READY_";
    unsigned char msg1[255];
    unsigned char msg2[255];
    unsigned char msg3[255];
    int msg1_len;
    int msg2_len;
    int msg3_len;
    unsigned char reply_msg[255];
    int reply_len;

    double aux_ff;

    struct sockaddr_in server_ad;
    struct sockaddr_in client_ad;
    SOCKET sd = INVALID_SOCKET;
    SOCKET sd2 = INVALID_SOCKET;
    SOCKET reply_sd = INVALID_SOCKET;
    int port;
    int client_port;
    int length;
    int n, i;
    u_long ioctlVal;
    char data[255];
    struct hostent *hote;
    char buf[200];
    int TCP_PROTO;
    int has_udp_peer = 0;
    char tcp_config_choice = 'n';
    WSADATA wsaData;
    float program_start_time = current_millis();
    float reply_values[6];
    struct sockaddr_in reply_ad;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        printf("ERROR: WSAStartup failed\n");
        return 1;
    }

    msg1_len = build_message_from_text_little_endian(msg1_str, msg1, (int)sizeof(msg1));
    msg2_len = build_message_from_float_array(msg2_str, 6, msg2, (int)sizeof(msg2));
    msg3_len = build_message_from_text_little_endian(msg3_str, msg3, (int)sizeof(msg3));
    if (msg1_len <= 0 || msg2_len <= 0 || msg3_len <= 0)
    {
        printf("ERROR: Failed to build outbound messages\n");
        WSACleanup();
        return 1;
    }

    printf("======= Simple Echo Server =======\n");
    printf("IP ADDRESS : %s\n", SERVER_IP_ADDRESS);
    printf("Select a port number for the IP server: ");
    if (scanf("%d", &port) != 1)
    {
        printf("ERROR: Invalid server port\n");
        WSACleanup();
        return 1;
    }

    printf("Select protocol (0=UDP, 1=TCP): ");
    if (scanf("%d", &TCP_PROTO) != 1 || (TCP_PROTO != 0 && TCP_PROTO != 1))
    {
        printf("ERROR: Invalid protocol selection\n");
        WSACleanup();
        return 1;
    }

    if (TCP_PROTO)
        sd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    else
        sd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (sd == INVALID_SOCKET)
    {
        printf("ERROR: Could not create socket\n");
        WSACleanup();
        return 1;
    }

    memset((char *)&server_ad, 0, sizeof(server_ad));
    server_ad.sin_family = AF_INET;
    if (strcmp(SERVER_IP_ADDRESS, "0.0.0.0") == 0)
        server_ad.sin_addr.s_addr = INADDR_ANY;
    else
        server_ad.sin_addr.s_addr = inet_addr(SERVER_IP_ADDRESS);
    server_ad.sin_port = htons((u_short)port);

    if (bind(sd, (struct sockaddr *)&server_ad, sizeof(server_ad)) == SOCKET_ERROR)
    {
        int err = WSAGetLastError();
        printf("ERROR: bind() failed (WSAGetLastError=%d)\n", err);
        if (err == WSAEADDRNOTAVAIL)
            printf("Hint: the configured IP %s is not assigned to any local interface.\n", SERVER_IP_ADDRESS);
        closesocket(sd);
        WSACleanup();
        return 1;
    }

    if (!TCP_PROTO)
    {
        if (create_bound_udp_socket(REPLY_IP_ADDRESS, REPLY_PORT, &reply_sd, &reply_ad) != 0)
        {
            printf("ERROR: Could not create/bind reply UDP socket on %s:%d\n", REPLY_IP_ADDRESS, REPLY_PORT);
            closesocket(sd);
            WSACleanup();
            return 1;
        }
    }

    gethostname(buf, sizeof(buf));
    hote = gethostbyname(buf);

    printf("======================================================\n");
    printf("SERVER:   Address  : %s\n", inet_ntoa(server_ad.sin_addr));
    if (hote && hote->h_name)
        printf("          Name     : %s\n", hote->h_name);
    else
        printf("          Name     : (unknown)\n");
    printf("          Port     : %d\n", port);
    if (!TCP_PROTO)
        printf("REPLY:    Address  : %s\n          Port     : %d\n", inet_ntoa(reply_ad.sin_addr), REPLY_PORT);
    printf("for msg1 text: %s | floats(LE): [", msg1_str);
    for (int i = 0; i < (int)(1 + strlen(msg1_str) * 4); i++)
    {
        uint8_t f;
        memcpy(&f, &msg1[i], 1);
        printf("  %2X", f);
    }
    printf(" ]\n");
    printf("for msg2 floats(LE): [");
    for (int i = 0; i < (int)(1 + (sizeof(msg2_str) / sizeof(msg2_str[0])) * 4); i++)
    {
        uint8_t f;
        memcpy(&f, &msg2[i], 1);
        printf("  %2X", f);
    }
    printf(" ]\n");
    printf("Protocol note: first transmitted byte is payload size (N), followed by N bytes of float data.\n");
    printf("When a client replies with READY_, the server enters a 0.5s receive/reply window.\n");
    printf("Normal mode: press 1 to send ByeBye, press 2 to send the predefined 6-float packet.\n");
    printf("To exit, press 'q'\n");
    printf("======================================================\n\n");

    if (TCP_PROTO)
    {
        if (listen(sd, 5) == SOCKET_ERROR)
        {
            printf("ERROR: Could not listen for connections\n");
            closesocket(sd);
            WSACleanup();
            return 1;
        }

        printf("Listening for incoming connections...\n");
        length = sizeof(client_ad);
        sd2 = accept(sd, (struct sockaddr *)&client_ad, &length);
        if (sd2 == INVALID_SOCKET)
        {
            printf("ERROR: Could not accept connection\n");
            closesocket(sd);
            WSACleanup();
            return 1;
        }

        printf("Client %s connected.\n", inet_ntoa(client_ad.sin_addr));

        ioctlVal = 1;
        if (ioctlsocket(sd2, FIONBIO, &ioctlVal) == SOCKET_ERROR)
        {
            printf("ERROR: Could not set non-blocking mode\n");
            closesocket(sd2);
            closesocket(sd);
            WSACleanup();
            return 1;
        }
    }
    else
    {
        printf("Use TCP-like configuration for UDP (wait for first packet)? (y/n): ");
        tcp_config_choice = (char)_getch();
        printf("\n");

        memset((char *)&client_ad, 0, sizeof(client_ad));
        client_ad.sin_family = AF_INET;

        if (tcp_config_choice != 'y' && tcp_config_choice != 'Y')
        {
            char client_ip[64];
            printf("Enter known UDP client IP (or 0 to skip): ");
            if (scanf("%63s", client_ip) == 1 && strcmp(client_ip, "0") != 0)
            {
                printf("Enter known UDP client port: ");
                if (scanf("%d", &client_port) == 1 && client_port > 0 && client_port < 65536)
                {
                    client_ad.sin_addr.s_addr = inet_addr(client_ip);
                    client_ad.sin_port = htons((u_short)client_port);
                    has_udp_peer = 1;
                    printf("UDP client preset: %s:%d\n", client_ip, client_port);
                }
            }
            else
            {
                printf("Will learn client address from first packet.\n");
            }
        }
        else
        {
            printf("Using TCP-like mode: will learn UDP client from first packet.\n");
        }

        ioctlVal = 1;
        if (ioctlsocket(sd, FIONBIO, &ioctlVal) == SOCKET_ERROR)
        {
            printf("ERROR: Could not set non-blocking mode\n");
            closesocket(sd);
            WSACleanup();
            return 1;
        }
    }

    {
        int receive_reply_mode = 0;

        while (1)
        {
            SOCKET normal_sd = TCP_PROTO ? sd2 : sd;
            SOCKET reply_mode_sd = TCP_PROTO ? sd2 : reply_sd;
            SOCKET active_sd = receive_reply_mode ? reply_mode_sd : normal_sd;
            int ready;

            if (_kbhit())
            {
                char c = (char)_getch();

                if (c == 'q')
                {
                    printf("Exiting server...\n");
                    break;
                }

                if (!receive_reply_mode)
                {
                    if (!TCP_PROTO && !has_udp_peer)
                    {
                        printf("No UDP peer known yet. Send/receive one packet first or preset client IP/port.\n");
                    }
                    else if (c == '1')
                    {
                        if (send_packet(active_sd, TCP_PROTO, msg1, msg1_len, &client_ad) == SOCKET_ERROR)
                        {
                            int e = WSAGetLastError();
                            printf("send failed (WSAGetLastError=%d)\n", e);
                        }
                        else
                        {
                            printf("Sending msg1 (%d bytes incl. length header).\n", msg1_len);
                        }
                    }
                    else if (c == '2')
                    {
                        if (send_packet(active_sd, TCP_PROTO, msg2, msg2_len, &client_ad) == SOCKET_ERROR)
                        {
                            int e = WSAGetLastError();
                            printf("send failed (WSAGetLastError=%d)\n", e);
                        }
                        else
                        {
                            printf("Sending msg2 (%d bytes incl. length header).\n", msg2_len);
                        }
                    }
                    else if (c == '3')
                    {
                        if (send_packet(active_sd, TCP_PROTO, msg3, msg3_len, &client_ad) == SOCKET_ERROR)
                        {
                            int e = WSAGetLastError();
                            printf("send failed (WSAGetLastError=%d)\n", e);
                        }
                        else
                        {
                            printf("Sending msg3 (%d bytes incl. length header).\n", msg3_len);
                            printf("READY_ sended: entering reply mode.\n");
                            receive_reply_mode = 1;
                            active_sd = reply_mode_sd;
                        }
                    }
                }
            }

            ready = socket_wait_readable(active_sd, receive_reply_mode ? 500 : 100);
            if (ready == SOCKET_ERROR)
            {
                printf("select() failed while waiting for incoming data.\n");
                receive_reply_mode = 0;
                continue;
            }

            if (ready == 0)
            {
                if (receive_reply_mode)
                {
                    printf("No more messages for 0.5 seconds, returning to send mode.\n");
                    receive_reply_mode = 0;
                }
                continue;
            }

            length = sizeof(client_ad);
            n = receive_packet(active_sd, TCP_PROTO, (unsigned char *)data, (int)sizeof(data), &client_ad, &length);
            if (n == SOCKET_ERROR)
            {
                int e = WSAGetLastError();
                printf("recv/recvfrom failed (WSAGetLastError=%d)\n", e);
                continue;
            }

            if (!TCP_PROTO && n > 0)
                has_udp_peer = 1;

            if (n > 0)
            {
                int ready_received;

                ready_received = log_received_packet((unsigned char *)data, n, &aux_ff);

                if (ready_received && !receive_reply_mode)
                {
                    printf("READY_ received: entering reply mode.\n");
                    receive_reply_mode = 1;
                    active_sd = reply_mode_sd;
                }

                if (receive_reply_mode)
                {
                    // time_t reply_now;

                    // reply_now = time(NULL);
                    update_msg2_values(reply_values, program_start_time);
                    // reply_values[0] = (float)difftime(reply_now, program_start_time);
                    // reply_values[1] = 1.5f;
                    // reply_values[2] = 1.5f;
                    // reply_values[3] = 1.5f;
                    // reply_values[4] = (float) 2500.0;
                    // reply_values[5] = 0.0f;
                    reply_len = build_message_from_float_array(reply_values, 6, reply_msg, (int)sizeof(reply_msg));
                    if (reply_len <= 0)
                    {
                        printf("ERROR: Failed to rebuild dynamic float message\n");
                        receive_reply_mode = 0;
                        continue;
                    }

                    if (send_packet(active_sd, TCP_PROTO, reply_msg, reply_len, &client_ad) == SOCKET_ERROR)
                    {
                        int e = WSAGetLastError();
                        printf("reply send failed (WSAGetLastError=%d)\n", e);
                    }
                    else
                    {
                        
                        printf("Reply: [%f, %f, %f, %f, %f, %f]\n", reply_values[0], reply_values[1], reply_values[2], reply_values[3], reply_values[4], reply_values[5]);
                        printf("Reply payload %d (LE): [",reply_len);
                        for (int i = 0; i < reply_len; i++)
                        {
                            printf("%02x ", reply_msg[i]);
                        }
                        printf("]\n");
                    }
                }
            }
        }
    }

    if (sd2 != INVALID_SOCKET)
        closesocket(sd2);
    if (reply_sd != INVALID_SOCKET)
        closesocket(reply_sd);
    if (sd != INVALID_SOCKET)
        closesocket(sd);

    WSACleanup();
    return 0;
}
// ___EOF___
