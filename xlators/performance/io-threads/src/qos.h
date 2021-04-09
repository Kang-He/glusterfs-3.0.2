/*
  QoS
*/
#include "compat-errno.h"
#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "common-utils.h"
#include "list.h"
#include <stdlib.h>
#include "locking.h"
#include <semaphore.h>
#include "io-threads.h"

#define CMP_UUID(origin_uuid, app_uuid) (strncmp(origin_uuid + UUID_OFFSET, app_uuid, strlen(app_uuid)))

typedef enum {
		APP_ACTIVE,
        APP_SLEEP
}APP_STATE;


struct app_qos_info
{
	struct list_head apps;
	int queue_size;
	int req_count;
	////double block_size;
	//int queue_size;
	double exp_bandwidth;	//MB/s
	double limit;			//MB/s
	double weight;
	double limit_bandwidth;	//KB/s
	double reserve_bandwidth;	//KB/s
	//double cur_weight;
	char uuid[MAX_APP_IDENTIFY];
	pthread_mutex_t mutex;
	//gf_boolean_t is_active;
	//gf_boolean_t block;
	struct timeval last_time;	//limit tag
	struct timeval last_reserve_time;	//reserve tag
	////TB tb;
	APP_STATE state;
	int IS_IOPS;
	double block_size;
};


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


typedef struct app_qos_info APP_Qos_Info;
typedef struct struct_client_id client_id_t;

#define UUID_OFFSET 0
#define DEFAULT_RESERVE 20
#define DEFAULT_LIMIT 1000
#define IOT_MAX_BANDWIDTH (450*1024)    //Maximum bandwidth MB / s, all converted to KB / s

#define list_for_each_back_entry(pos, head, member)				\
	for (pos = list_entry((head)->prev, typeof(*pos), member);	\
	     &pos->member != (head); 					\
	     pos = list_entry(pos->member.prev, typeof(*pos), member))


int 
iot_qos_set_next_limit(APP_Qos_Info *app, call_stub_t *stub);

int 
iot_qos_set_next_reserve(APP_Qos_Info *app, call_stub_t *stub);

APP_Qos_Info*
iot_qos_client_exist(const char* client_uuid, iot_conf_t *conf);

APP_Qos_Info*
iot_qos_app_info_insert(iot_conf_t *conf, double bandwidth, double limit, const char* uuid, int IS_IOPS, double block_size);

int
iot_qos_notify_wait (iot_conf_t *conf, struct timeval idletime);

int
iot_qos_notify_limit_wait (limit_worker *worker, struct timeval idletime);

int
iot_qos_is_over_limit_time(struct timeval limit_time);

void
iot_qos_app_reweight(iot_conf_t * conf);

void
iot_qos_eviction_and_print_app_throughput(iot_conf_t * conf);

void
set_app_bw(iot_conf_t * conf, const char* const msg, double v_wbw, double v_rbw, double v_diops);



