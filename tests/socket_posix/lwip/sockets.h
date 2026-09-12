#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <unistd.h>
#define lwip_socket socket
#define lwip_ioctl ioctl
#define lwip_select select
#define lwip_bind bind
#define lwip_connect connect
#define lwip_getsockopt getsockopt
#define lwip_close close
#define lwip_send send
#define lwip_sendto sendto
#define lwip_recv recv
#define lwip_recvfrom recvfrom
