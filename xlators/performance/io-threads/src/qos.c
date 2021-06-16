/*
  QoS
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "call-stub.h"
#include <sys/time.h>
#include <time.h>

#include "qos.h"
#include "redis_subscriber.h"


/*
*	_gf_false: now < limit_time
*	_gf_true: limit_time < now
*/
int
iot_qos_set_limit_time(struct timeval *last_time, struct timeval *now, double gap){
	double difftime = (now->tv_sec - last_time->tv_sec) * 1000000.0 + now->tv_usec - last_time->tv_usec;
	int ret = _gf_true;
	
	if(difftime < 0){
		ret = _gf_false;
	}
	gap += last_time->tv_usec;
	while(gap >= 1000000.0){
		last_time->tv_sec += gap / 1000000.0;
		gap -= 1000000.0;
	}
	last_time->tv_usec = gap;

	*now = *last_time;

	return ret;
}

/*
*	_gf_false: now > reserve_time
*	_gf_true: reserve_time > now
*/
int
iot_qos_set_reserve_time(struct timeval *last_time, struct timeval *now, double gap){
	double difftime = (now->tv_sec - last_time->tv_sec) * 1000000.0 + now->tv_usec - last_time->tv_usec;
	int ret = _gf_true;
	if(difftime > 0){
		*last_time = *now;
		ret = _gf_false;
	}
	gap += last_time->tv_usec;
	while(gap >= 1000000.0){
		last_time->tv_sec += gap / 1000000.0;
		gap -= 1000000.0;
	}
	last_time->tv_usec = gap;

	*now = *last_time;
	
	return ret;
}



/*In testing, we have always defaulted the request size to 128KB
* 	bandwidth all converted to KB / s
*/
double
iot_qos_get_gap(double bandwidth, double size){
	double req_size = 128;	//因为GlusterFS会把大于128K的块切成等于128K
	//req_size = size;
	
	double time = 1000000 * req_size / bandwidth;
	
	return time;
}

int 
iot_qos_set_next_limit(APP_Qos_Info *app, call_stub_t *stub){
	if(app == NULL) return _gf_false;
	
	//struct timeval *now = &stub->limit_time;
	if(stub->fop == GF_FOP_WRITE){
		double gap = iot_qos_get_gap(app->write_limit_bandwidth, app->write_block_size / 1024);
		return iot_qos_set_limit_time(&app->write_last_time, &stub->limit_time, gap);
	}	
	else{
		double gap = iot_qos_get_gap(app->read_limit_bandwidth, app->read_block_size / 1024);
		return iot_qos_set_limit_time(&app->read_last_time, &stub->limit_time, gap);
	}

	//gf_log(stub->frame->this->name, GF_LOG_ERROR, "jy_message_set_next_timeval: gap: %f us", gap);
	
}

int 
iot_qos_set_next_reserve(APP_Qos_Info *app, call_stub_t *stub){
	if(app == NULL) return _gf_false;
	int ret;

	//struct timeval *now = &stub->reserve_time;
	if(stub->fop == GF_FOP_WRITE){
		double gap = iot_qos_get_gap(app->write_reserve_bandwidth, app->write_block_size / 1024);
		ret = iot_qos_set_reserve_time(&app->write_last_reserve_time, &stub->reserve_time, gap);
		if(ret == _gf_false)
			app->write_last_time = stub->limit_time;
	}
	else{
		double gap = iot_qos_get_gap(app->read_reserve_bandwidth, app->read_block_size / 1024);
		ret = iot_qos_set_reserve_time(&app->read_last_reserve_time, &stub->reserve_time, gap);
		if(ret == _gf_false)
			app->read_last_time = stub->limit_time;
	}
	

	return ret;
}


int
iot_qos_get_uuid(const char* src_uuid, char* uuid){
	if(src_uuid == NULL || uuid == NULL) return -1;
	int i = UUID_OFFSET;
	while(src_uuid[i] != '\0' && src_uuid[i] != '.'){
		i++;
	}
	int len = i - UUID_OFFSET;
	if(len > MAX_APP_IDENTIFY) return -1;
	//gf_log(uuid, GF_LOG_ERROR, "jy_message:len=%d", len);
	strncpy(uuid, src_uuid + UUID_OFFSET, len);
	
	//jy_test
	gf_log(uuid, GF_LOG_ERROR, "jy_message_get_uuid:uuid=%s", uuid);
	//jy_test

	return 0;
}

APP_Qos_Info*
iot_qos_client_exist(const char* client_uuid, iot_conf_t *conf){
	if(conf->app_count <= 0) return NULL;
	APP_Qos_Info *tmp_app;
	int i = 1;

	/*
	tmp_app = list_first_entry(&conf->apps, APP_Qos_Info, apps);
	
	for(; i <= conf->app_count; i++){
		if(CMP_UUID(client_uuid, tmp_app->uuid) == 0) break;
		tmp_app = list_first_entry(&tmp_app->apps, APP_Qos_Info, apps);
	}
	*/
	list_for_each_entry (tmp_app, &conf->apps, apps){
        if(CMP_UUID(client_uuid, tmp_app->uuid) == 0) break;
        i++;
    }
	
	if(i <= conf->app_count) {
		/*
		if(tmp_app->state == APP_SLEEP){
			list_del_init(&tmp_app->apps);
			tmp_app->state = APP_ACTIVE;
			list_add(&tmp_app->apps, &conf->apps);
		}
		*/
		return tmp_app;
	}
	return NULL;
}

/*APP_Qos_Info*
iot_qos_app_info_insert_with_only_limit(iot_conf_t *conf, double bandwidth, const char* uuid){
	if(conf == NULL || bandwidth <= 0) return NULL;
	APP_Qos_Info *new_app;
	int ret = 0;
	//double total_weight = 0;
	//double rate;
	
	new_app = (APP_Qos_Info *) CALLOC(1, sizeof(APP_Qos_Info));
	if(new_app == NULL){
		gf_log(conf->this->name, GF_LOG_ERROR, "CALLOC APP_Qos_Info error");
		return NULL;
	}

	if ((ret = pthread_mutex_init(&new_app->mutex, NULL)) != 0) {
        gf_log(conf->this->name, GF_LOG_ERROR,  "pthread_mutex_init failed (%d)", ret);
        return NULL;
    }
	
	INIT_LIST_HEAD(&new_app->apps);
	new_app->exp_bandwidth = bandwidth;
	int i = 0;
	for(; i < MAX_APP_IDENTIFY; i++){
		new_app->uuid[i] = '\0';
	}

	ret = iot_qos_get_uuid(uuid, new_app->uuid);
	if(ret != 0) {
		free(new_app);
		return NULL;
	}

	if(conf->app_count == 0){
		new_app->weight = 1;
		new_app->limit_bandwidth = conf->default_BW;
	}
	else{
		double total_weight = bandwidth;
        //pthread_mutex_lock(&conf->otlock);
		{
			APP_Qos_Info *tmp_app;
			list_for_each_entry (tmp_app, &conf->apps, apps){
				total_weight += tmp_app->exp_bandwidth;
			}
			list_for_each_entry (tmp_app, &conf->apps, apps){
				tmp_app->weight = tmp_app->exp_bandwidth / total_weight;
				tmp_app->limit_bandwidth = tmp_app->weight * conf->default_BW;
				gf_log(conf->this->name, GF_LOG_ERROR, "jy_message_app_insert: reset app:%s limit_bandwidth to %f MB/s", tmp_app->uuid, tmp_app->limit_bandwidth / 1024);
			}
        }
        //pthread_mutex_unlock(&conf->otlock);
		
		new_app->weight = new_app->exp_bandwidth / total_weight;
		new_app->limit_bandwidth = new_app->weight * conf->default_BW;
	}

	//struct timeval now;
	//gettimeofday(&now,NULL);
	//new_app->last_time = now;
	gettimeofday(&new_app->last_time,NULL);
	new_app->last_reserve_time = new_app->last_time;
	
	list_add_tail(&new_app->apps, &conf->apps);
	new_app->queue_size = 0;
	new_app->req_count = 0;
	new_app->state = APP_ACTIVE;
	//new_app->reserve_flag = _gf_true;
	//new_app->is_active = _gf_true;
	//new_app->queue_size = 0;
	//new_app->block = _gf_false;
	conf->app_count++;
	//conf->active_app_count++;
	//jy_test
	gf_log(conf->this->name, GF_LOG_ERROR, "jy_message_app_insert: insert app:%s, app_count:%d, app_limit_bandwidth:%f MB/s", new_app->uuid, conf->app_count, new_app->limit_bandwidth / 1024);
	//gf_log(conf->this->name, GF_LOG_ERROR, "jy_message_app_insert:active_app:%d, app:%d", conf->active_app_count, conf->app_count);
	//jy_test

	return new_app;
}*/

APP_Qos_Info*
iot_qos_app_info_insert(iot_conf_t *conf, double bandwidth, double limit, const char* uuid, int IS_IOPS, double block_size){
	gf_log(conf->this->name, GF_LOG_ERROR, "jy_message: insert app with reserve:%fMB/s, limit:%fMB/s", bandwidth, limit);
	if(conf == NULL || bandwidth <= 0 || limit <  bandwidth) return NULL;
	APP_Qos_Info *new_app;
	int ret = 0;

	
	new_app = (APP_Qos_Info *) CALLOC(1, sizeof(APP_Qos_Info));
	if(new_app == NULL){
		gf_log(conf->this->name, GF_LOG_ERROR, "CALLOC APP_Qos_Info error");
		return NULL;
	}
	
	if ((ret = pthread_mutex_init(&new_app->mutex, NULL)) != 0) {
        gf_log(conf->this->name, GF_LOG_ERROR,  "pthread_mutex_init failed (%d)", ret);
        return NULL;
    }

	new_app->IS_IOPS = IS_IOPS;
	new_app->write_block_size = block_size;
	//add_read
	new_app->read_block_size = block_size;
	INIT_LIST_HEAD(&new_app->apps);
	new_app->read_exp_bandwidth = bandwidth;
	new_app->read_limit = limit;
	new_app->write_exp_bandwidth = bandwidth;
	new_app->write_limit = limit;
	//add_read
	int i = 0;
	for(; i < MAX_APP_IDENTIFY; i++){
		new_app->uuid[i] = '\0';
	}

	ret = iot_qos_get_uuid(uuid, new_app->uuid);
	if(ret != 0) {
		free(new_app);
		return NULL;
	}
	//add_read
	new_app->read_reserve_bandwidth = bandwidth * 1024;
	new_app->write_reserve_bandwidth = bandwidth * 1024;
	if(conf->app_count == 0){
		new_app->read_weight = 1;
		new_app->read_limit_bandwidth = conf->default_BW;
		new_app->write_weight = 1;
		new_app->write_limit_bandwidth = conf->default_BW;
		//new_app->limit_bandwidth = IOT_MAX_BANDWIDTH;
	}
	else{
		double read_total_weight = bandwidth;
		double write_total_weight = bandwidth;
		{
			APP_Qos_Info *tmp_app;
			list_for_each_entry (tmp_app, &conf->apps, apps){
				if(tmp_app->state == APP_SLEEP)
					break;
				read_total_weight += tmp_app->read_exp_bandwidth;
				write_total_weight += tmp_app->write_exp_bandwidth;
			}
			list_for_each_entry (tmp_app, &conf->apps, apps){
				if(tmp_app->state == APP_SLEEP)
					break;
				pthread_mutex_lock(&tmp_app->mutex);
				tmp_app->read_weight = tmp_app->read_exp_bandwidth / read_total_weight;
				tmp_app->read_limit_bandwidth = tmp_app->read_weight * conf->default_BW;
				if(tmp_app->read_limit_bandwidth <= tmp_app->read_reserve_bandwidth){
					tmp_app->read_reserve_bandwidth = tmp_app->read_limit_bandwidth / 2.0;
					gf_log(conf->this->name, GF_LOG_ERROR, "jy_message_app_insert: app:%s read_limit < read_reserve!!!!!", tmp_app->uuid);
				}

				tmp_app->write_weight = tmp_app->write_exp_bandwidth / write_total_weight;
				tmp_app->write_limit_bandwidth = tmp_app->write_weight * conf->default_BW;
				if(tmp_app->write_limit_bandwidth <= tmp_app->write_reserve_bandwidth){
					tmp_app->write_reserve_bandwidth = tmp_app->write_limit_bandwidth / 2.0;
					gf_log(conf->this->name, GF_LOG_ERROR, "jy_message_app_insert: app:%s write_limit < write_reserve!!!!!", tmp_app->uuid);
				}
				pthread_mutex_unlock(&tmp_app->mutex);
				//add_read
				gf_log(conf->this->name, GF_LOG_ERROR, "jy_message_app_insert: reset app:%s write_limit_bandwidth to %f MB/s, read_limit_bandwidth to %f MB/s", tmp_app->uuid, tmp_app->write_limit_bandwidth / 1024, tmp_app->read_limit_bandwidth / 1024);
			}
        }
		
		new_app->read_weight = new_app->read_exp_bandwidth / read_total_weight;
		new_app->read_limit_bandwidth = new_app->read_weight * conf->default_BW;
		if(new_app->read_limit_bandwidth <= new_app->read_reserve_bandwidth){
			new_app->read_reserve_bandwidth = new_app->read_limit_bandwidth / 2.0;
			gf_log(conf->this->name, GF_LOG_ERROR, "jy_message_app_insert: app:%s read_limit < read_reserve!!!!!", new_app->uuid);
		}

		new_app->write_weight = new_app->write_exp_bandwidth / write_total_weight;
		new_app->write_limit_bandwidth = new_app->write_weight * conf->default_BW;
		if(new_app->write_limit_bandwidth <= new_app->write_reserve_bandwidth){
			new_app->write_reserve_bandwidth = new_app->write_limit_bandwidth / 2.0;
			gf_log(conf->this->name, GF_LOG_ERROR, "jy_message_app_insert: app:%s write_limit < write_reserve!!!!!", new_app->uuid);
		}
	}
	//add_read
	//new_app->limit_bandwidth = limit * 1024;

	//add_read
	gettimeofday(&new_app->read_last_time,NULL);
	new_app->read_last_reserve_time = new_app->read_last_time;
	gettimeofday(&new_app->write_last_time,NULL);
	new_app->write_last_reserve_time = new_app->write_last_time;
	new_app->state = APP_ACTIVE;
	
	//list_add_tail(&new_app->apps, &conf->apps);
	list_add(&new_app->apps, &conf->apps);
	new_app->queue_size = 0;

	conf->app_count++;
	//add_read
	gf_log(conf->this->name, GF_LOG_ERROR, "jy_message_app_insert: insert app:%s, app_count:%d, app_write_reserve_bandwidth:%f MB/s, app_write_limit_bandwidth:%f MB/s, app_read_reserve_bandwidth:%f MB/s, app_read_limit_bandwidth:%f MB/s", new_app->uuid, conf->app_count, new_app->write_exp_bandwidth, new_app->write_limit_bandwidth  / 1024, new_app->read_exp_bandwidth, new_app->read_limit_bandwidth  / 1024);
	


	return new_app;
}

int
iot_qos_notify_wait (iot_conf_t *conf, struct timeval idletime)
{
        //struct timeval  tv;
        struct timespec ts = {0, };
        int waitres = 0;

        //gettimeofday (&tv, NULL);
        /* Slightly skew the idle time for threads so that, we dont
         * have all of them rushing to exit at the same time, if
         * they've been idle.
         */
        //ts.tv_sec = skew_sec_idle_time (tv.tv_sec + idletime);
        ts.tv_nsec = idletime.tv_usec * 1000;
		ts.tv_sec = idletime.tv_sec;
		

        waitres = pthread_cond_timedwait (&conf->ot_notifier, &conf->otlock,
                                          &ts);

        return waitres;
}


int
iot_qos_notify_limit_wait (limit_worker *worker, struct timeval idletime)
{
        struct timespec ts = {0, };
        int waitres = 0;
		
        ts.tv_nsec = idletime.tv_usec * 1000;
		ts.tv_sec = idletime.tv_sec;

        waitres = pthread_cond_timedwait (&worker->notifier, &worker->qlock,
                                          &ts);
        return waitres;
}

/*
*	_gf_false:	now <= limit_time
*	_gf_true:	now > limit_time
*/
int
iot_qos_is_over_limit_time(struct timeval limit_time){
	struct timeval now;
	gettimeofday(&now,NULL);

	double difftime = (now.tv_sec - limit_time.tv_sec) * 1000000.0 + now.tv_usec - limit_time.tv_usec;
	if(difftime >= 0) return _gf_true;
	return _gf_false;
}

call_stub_t*
iot_qos_priority_dequeue_ordered(iot_conf_t *  conf){
	if(list_empty(&conf->priority_req))
		return NULL;
	call_stub_t *stub = NULL;
	

	list_for_each_entry (stub, &conf->priority_req, list)
        break;

    list_del_init(&stub->list);
		
	if(stub->app != NULL && stub->fop == GF_FOP_WRITE){

		APP_Qos_Info* app = (APP_Qos_Info*) stub->app;
		//gf_log(conf->this->name, GF_LOG_ERROR, "jy_message: deal priority req from app: %s stub:%d", app->uuid, stub);
		pthread_mutex_lock(&app->mutex);
    	{
			app->queue_size--;
			//list_del_init(&stub->req);
		}
		pthread_mutex_unlock(&app->mutex);
	}

	return stub;
}

void
iot_qos_app_reweight(iot_conf_t * conf){
	double write_total_weight = 0;
	double read_total_weight = 0;
	APP_Qos_Info *tmp_app;
	
	list_for_each_entry (tmp_app, &conf->apps, apps){
		if(tmp_app->state == APP_SLEEP)
				break;
		//add_read
		write_total_weight += tmp_app->write_exp_bandwidth;
		read_total_weight += tmp_app->read_exp_bandwidth;
	}
	
	list_for_each_entry (tmp_app, &conf->apps, apps){
		if(tmp_app->state == APP_SLEEP)
					break;
		pthread_mutex_lock(&tmp_app->mutex);
		//add_read
		//for write
		tmp_app->write_reserve_bandwidth = tmp_app->write_exp_bandwidth * 1024;
		tmp_app->write_weight = tmp_app->write_exp_bandwidth / write_total_weight;
		tmp_app->write_limit_bandwidth = tmp_app->write_weight * conf->default_BW;
		if(tmp_app->write_limit_bandwidth <= tmp_app->write_reserve_bandwidth){
			tmp_app->write_reserve_bandwidth = tmp_app->write_limit_bandwidth / 2.0;
			gf_log(conf->this->name, GF_LOG_ERROR, "jy_message_app_reweight: app:%s write limit < reserve!!!!!", tmp_app->uuid);
		}
		gettimeofday(&tmp_app->write_last_time,NULL);
		//for read
		tmp_app->read_reserve_bandwidth = tmp_app->read_exp_bandwidth * 1024;
		tmp_app->read_weight = tmp_app->read_exp_bandwidth / read_total_weight;
		tmp_app->read_limit_bandwidth = tmp_app->read_weight * conf->default_BW;
		if(tmp_app->read_limit_bandwidth <= tmp_app->read_reserve_bandwidth){
			tmp_app->read_reserve_bandwidth = tmp_app->read_limit_bandwidth / 2.0;
			gf_log(conf->this->name, GF_LOG_ERROR, "jy_message_app_reweight: app:%s read limit < reserve!!!!!", tmp_app->uuid);
		}
		gettimeofday(&tmp_app->read_last_time,NULL);
		pthread_mutex_unlock(&tmp_app->mutex);
	
		gf_log(conf->this->name, GF_LOG_ERROR, "jy_message_app_reweight: reweight app:%s write_limit_bandwidth to %f MB/s, read_limit_bandwidth to %f MB/s", tmp_app->uuid, tmp_app->write_limit_bandwidth / 1024, tmp_app->read_limit_bandwidth / 1024);
	}
	
}


void
iot_qos_eviction_and_print_app_throughput(iot_conf_t * conf){
	APP_Qos_Info *tmp_app;
	int eviction = _gf_false;
	
	list_for_each_entry (tmp_app, &conf->apps, apps){
		if(tmp_app->state == APP_SLEEP)
			break;
		//print
		//gf_log(conf->this->name, GF_LOG_ERROR, "jy_message: app: %s throughput: %d", tmp_app->uuid, tmp_app->req_count);

		//eviction
		while(tmp_app->queue_size == 0 && tmp_app->req_count == 0){
			eviction = _gf_true;
			APP_Qos_Info *next_app;
			list_for_each_entry (next_app, &tmp_app->apps, apps)
				break;
			
			//pthread_mutex_destroy(&tmp_app->mutex);
			list_del_init(&tmp_app->apps);
			tmp_app->state = APP_SLEEP;
			list_add_tail(&tmp_app->apps, &conf->apps);
			//conf->app_count --;
			//free(tmp_app);
			
			if((&next_app->apps) == (&conf->apps) || next_app->state == APP_SLEEP)
				goto out;
			tmp_app = next_app;
		}

		pthread_mutex_lock(&tmp_app->mutex);
		tmp_app->req_count = 0;
		pthread_mutex_unlock(&tmp_app->mutex);
	}

out:
	if(conf->app_count > 0 && eviction == _gf_true)
		iot_qos_app_reweight(conf);
	
}

void
iot_qos_app_delete(iot_conf_t * conf, const char* const hostname){
	APP_Qos_Info *tmp_app;
	int delete = _gf_false;
	char appname[60];
	iot_qos_get_uuid(hostname, appname);
	list_for_each_entry (tmp_app, &conf->apps, apps){
		if(strncmp(tmp_app->uuid, appname, strlen(appname)) == 0){
			delete = _gf_true;
			list_del_init(&tmp_app->apps);
			conf->app_count--;
			pthread_mutex_destroy(&tmp_app->mutex);
			free(tmp_app);
			gf_log(conf->this->name, GF_LOG_ERROR, "jy_message: delete app %s sucess", appname);
			break;
		}
	}
	if(conf->app_count > 0 && delete == _gf_true)
		iot_qos_app_reweight(conf);	
}

void
set_app_bw(iot_conf_t * conf, const char* const msg, double v_wbw, double v_rbw, double v_diops){
	if(conf == NULL || msg == NULL) return;
	if(v_wbw == -1 && v_rbw == -1 && v_diops == -1) return;
	
	pthread_mutex_lock(&conf->otlock);
	APP_Qos_Info *app = iot_qos_client_exist(msg, conf);
	if(app == NULL){
		gf_log(conf->this->name, GF_LOG_ERROR, "jy_message: no match app");
		goto unlock_out;
	}
	pthread_mutex_lock(&app->mutex);

	//no support for rbw
	//add_read
	if(v_wbw != -1){
		app->write_exp_bandwidth = v_wbw;
	}
	else if(v_diops != -1){
		if(app->IS_IOPS != 1)
			app->IS_IOPS = 1;
		app->write_exp_bandwidth = v_diops * (app->write_block_size / (1024 * 1024));	//IOPS * block_size = B/s
	}
	else if(v_rbw != -1){
		app->read_exp_bandwidth = v_rbw;
	}
	
	pthread_mutex_unlock(&app->mutex);
	gf_log(conf->this->name, GF_LOG_ERROR, "jy_message: set app %s new_bandwidth: %f", app->uuid, v_wbw);
	iot_qos_app_reweight(conf);

unlock_out:
	pthread_mutex_unlock(&conf->otlock);
	return;
}


