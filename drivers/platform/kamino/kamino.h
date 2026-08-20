/*
 * kamino.h
 *
 */

#ifndef KAMINO_H_
#define KAMINO_H_

unsigned int boot_complete_get_bid(void);
#define KAMINO_LOG(fmt, ...)                                                \
do {																	    \
	printk(KERN_ERR"KML:[%u] "fmt, boot_complete_get_bid(), ##__VA_ARGS__);	\
} while(0)

#endif /* KAMINO_H_ */
