#ifndef __SKALL_H__
#define __SKALL_H__


#include <c_types.h>


// like recv() with MSG_WAITALL flag. returns true if buffer was filled
// before EOF/error.
bool recvall(int s, uint8_t *buf, size_t size);

// like send() with MSG_WAITALL flag. returns true if buffer was completely sent
// with no errors.
bool sendall(int s, const uint8_t *buf, size_t size);


#endif
