#ifndef CRedisSubscriber_H_INCLUDED
#define CRedisSubscriber_H_INCLUDED
	
#include <hiredis/async.h>
#include <hiredis/adapters/libevent.h>
#include <pthread.h>
#include <semaphore.h>
#include "io-threads.h"



#define IP "10.10.1.1"
#define PORT 6379
#define MAX_TAG_IDENTIFY 20
#define MAX_TAG_NUM 8


typedef enum {
		APP_UN_DEFINE,
        APP_WBW,
        APP_RBW,
        APP_DIOPS
}TAG_TYPE;


typedef struct CRedisSubscriber {
	struct event_base *_event_base;
	pthread_t _event_thread;
	sem_t _event_sem;
	redisAsyncContext *_redis_context;
	struct iot_conf *conf;
	iot_state_t state;
}CRedisSubscriber;
	
	int sub_redis_init(void *pthis, iot_conf_t * conf);
	int sub_redis_uninit(void *pthis);
	int sub_redis_disconnect(void *pthis);
	int sub_redis_connect(void *pthis);
	int subscribe(const char *channel_name, void *pthis);
	
	void sub_connect_callback(const redisAsyncContext *redis_context,
		int status);
	
	void sub_disconnect_callback(const redisAsyncContext *redis_context,
		int status);
	
	void command_callback(redisAsyncContext *redis_context,
		void *reply, void *privdata);
	
	
	void *sub_event_thread(void *data);
	void *sub_event_proc(void *pthis);
	
#endif // CRedisSubscriber_H_INCLUDED


void *
sub_worker(iot_conf_t * conf);

void 
sub_exit(iot_conf_t * conf);


