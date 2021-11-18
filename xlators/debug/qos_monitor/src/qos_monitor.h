#ifndef _QOS_MONITOR_H
#define _QOS_MONITOR_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "xlator.h"
#include "defaults.h"
#include "list.h"
#include "dict.h"

#include <errno.h>
#include <stdlib.h>
#include <semaphore.h>
#include <pthread.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>


#include <hiredis/hiredis.h> 
#include <hiredis/async.h>
#include <hiredis/adapters/libevent.h>

// default option value 
#define HOST  "10.10.1.1"
#define PORT 6379
#define CHANNEL "monitor"
#define INTERNAL 10
// get client_id related macros
#define DELIMER "."
#define CLIENTID 60
#define TIMES 1
#define KB 1024
#define MB 1024*1024
#define MSGLEN 350

int32_t REDIS_INTERVAL = 1000;
int32_t PUBLISH_WAIT_SEC = 1;
int32_t DISCONNECT_FLAG = 0;
/* changed from struct _server_connection
* used for identity client
 */
struct struct_client_id {
	struct list_head    list;
	char               *id;
	int                 ref;
	int                 active_transports;
	pthread_mutex_t     lock;
	char                disconnected;
	fdtable_t          *fdtable; 
	struct _lock_table *ltable;
	xlator_t           *bound_xl;
};

typedef struct struct_client_id client_id_t;

typedef struct CRedisPublisher {
	//异步发布监控数据需要的
    struct event_base *_event_base; // libevent
    pthread_t _event_thread; 	    // event thread ID
    sem_t _event_sem;               // event thread 
    redisAsyncContext *_redis_context; // hiredis 

	//同步订阅计算节点和应用的映射关系
	pthread_t sub_pthread;
	redisContext *sub_redis_context;

	// redis related
	char *redis_host;   
	int redis_port;
	char *channel;

	//jy-test
	pthread_cond_t notifier;
    pthread_mutex_t qlock;
}CRedisPublisher;

struct qos_local {
	struct timeval wind_at;
	struct timeval unwind_at;
	double value;
};

struct qos_monitor_data {
	double data_written;
	double data_read;
	struct qos_local write_delay;
	struct qos_local read_delay;
	double data_iops;
	double create_iops;
	double open_iops;
	double unlink_iops;
	double stat_iops;
	struct timeval started_at;
};

struct qos_monitor_private {
        gf_lock_t lock;
		dict_t *metrics;
		dict_t *node_appname;
		CRedisPublisher *publisher;
		int32_t qos_monitor_interval;
		pthread_t monitor_thread;
		int monitor_thread_should_die;
		int monitor_thread_running;
		double reconnect_times;
		double reconnect_succed;
};
typedef struct qos_monitor_private qos_monitor_private_t;

void *event_proc(void *pthis);
void *event_thread(void *data);
void pubCallback(redisAsyncContext *c, void *r, void *priv);
void connectCallback(const redisAsyncContext *c, int status);
void disconnectCallback(const redisAsyncContext *c, int status);
int redis_init(void *pthis);
int redis_uninit(void *pthis);
int redis_disconnect(void *pthis);
int redis_connect(void *pthis);
int publish(const char *channel_name, const char *message, void *pthis);

#endif
