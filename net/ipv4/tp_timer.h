#ifndef _TP_TIMER_H
#define _TP_TIMER_H

#include <linux/time.h>
#include <linux/ip.h>

#define TPS_SOCK 1
#define TPS_SOCK_TRANS 2
#define TPS_TCP_IP 3
#define TPS_UDP_IP 13
#define TPS_IP_NET 4
#define TPS_NET 5
#define TPR_NET 6
#define TPR_NET_IP 7
#define TPR_IP_TCP 8
#define TPR_IP_UDP 18
#define TPR_TCP_SOCK 9
#define TPR_UDP_SOCK 19
#define TPR_SOCK 10

struct tp_timer_data {
       unsigned short id; // identifies probing point
       struct timeval ts; // microsecond timestamp
       unsigned int seq; // sequence number
       unsigned int threadnr; // number of thread
       unsigned short timesrepeated; // number of times, seq is repeated
       unsigned long count; // counter to detect missed logs
    };

void tp_timer_init(void);
void inline tp_timer_seq(const short id, struct sk_buff * skb);
void inline tp_timer_data(const short id, unsigned char * data, unsigned char * tail);

/**
    * this function's purpose is to log an identifier, combined with
    * a sequence number and an timestamp into a memory segment that can be
    * read out at any time.
    */
inline void tp_timer(const short id, const unsigned int seq, const unsigned int threadnr, const short timesrepeated);

#endif /* _TP_TIMER_H */

