#include <lwip/sockets.h>
#include "skall.h"


bool recvall(int s, uint8_t *buf, size_t size)
{
    int i = 0;
    while (i < size) {
        int bytes_left = size - i;

        int recvd = lwip_recv(s, buf + i, bytes_left, 0);
        if (recvd <= 0) {
            return false;
        }

        i += recvd;
    }

    return true;
}

bool sendall(int s, const uint8_t *buf, size_t size)
{
    int i = 0;
    while (i < size) {
        int bytes_left = size - i;

        int sent = lwip_send(s, buf + i, bytes_left, 0);
        if (sent <= 0) {
            return false;
        }

        i += sent;
    }

    return true;
}
