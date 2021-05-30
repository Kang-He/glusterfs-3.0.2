#include <stdlib.h>
#include <assert.h>
#include <hiredis/hiredis.h>
#include "redis_subscriber.h"
#include "qos.h"


int sub_redis_init(void *pthis, iot_conf_t * conf)
{
    CRedisSubscriber *p = (CRedisSubscriber *)pthis;

	p->conf = conf;

    p->_event_base = event_base_new();
    if (NULL == p->_event_base)
    {
        //printf(": Create redis event failed.\n");
		gf_log(p->conf->this->name, GF_LOG_ERROR, "jy_message: Create redis event failed ");
        return _gf_false;
    }

    memset(&p->_event_sem, 0, sizeof(p->_event_sem));
    int ret = sem_init(&p->_event_sem, 0, 0);
    if (ret != 0)
    {
        //printf(": Init sem failed.\n");
		gf_log(p->conf->this->name, GF_LOG_ERROR, "jy_message: Init sem failed ");
        return _gf_false;
    }

    return _gf_true;
}

int sub_redis_uninit(void *pthis)
{
    CRedisSubscriber *p = (CRedisSubscriber *)pthis;
	event_base_free(p->_event_base);
    p->_event_base = NULL;

    sem_destroy(&p->_event_sem);
    return _gf_true;
}

int sub_redis_disconnect(void *pthis)
{
    CRedisSubscriber *p = (CRedisSubscriber *)pthis;
    if (p->_redis_context)
    {
        redisAsyncDisconnect(p->_redis_context);
		redisAsyncFree(p->_redis_context);
        p->_redis_context = NULL;
    }

    return _gf_true;
}

int sub_redis_connect(void *pthis)
{
    CRedisSubscriber *p = (CRedisSubscriber *)pthis;

	iot_conf_t* conf = p->conf;

	if(conf->redis_host != NULL && conf->redis_port > 0){
		p->_redis_context = redisAsyncConnect(conf->redis_host, conf->redis_port);
	}
	else{
		gf_log(p->conf->this->name, GF_LOG_ERROR, "jy_message: sub : not set option for redis host and port ");
    	p->_redis_context = redisAsyncConnect(IP, PORT);    // Òì²½Á¬½Óµ½redis·þÎñÆ÷ÉÏ£¬Ê¹ÓÃÄ¬ÈÏ¶Ë¿Ú
	}
	
	if (NULL == p->_redis_context)
    {
        //printf(": Connect redis failed.\n");
		gf_log(p->conf->this->name, GF_LOG_ERROR, "jy_message: sub : Connect redis failed ");
        return _gf_false;
    }

    if (p->_redis_context->err)
    {
        printf(": Connect redis error: %d, %s\n",
            p->_redis_context->err, p->_redis_context->errstr);    // Êä³ö´íÎóÐÅÏ¢
		gf_log(p->conf->this->name, GF_LOG_ERROR, "jy_message: sub : Connect redis error ");
		return _gf_false;
    }

  
    redisLibeventAttach(p->_redis_context, p->_event_base);    // ½«ÊÂ¼þ°ó¶¨µ½redis contextÉÏ£¬Ê¹ÉèÖÃ¸øredisµÄ»Øµ÷¸úÊÂ¼þ¹ØÁª

   
    int ret = pthread_create(&p->_event_thread, 0, &sub_event_thread, p);
    if (ret != 0)
    {
        //printf(": create event thread failed.\n");
		gf_log(p->conf->this->name, GF_LOG_ERROR, "jy_message: sub : create event thread failed");
        sub_redis_disconnect(p);
        return _gf_false;
    }

	
    redisAsyncSetConnectCallback(p->_redis_context,
        &sub_connect_callback);

	
    redisAsyncSetDisconnectCallback(p->_redis_context,
        &sub_disconnect_callback);


    sem_post(&p->_event_sem);
    return _gf_true;
}

int subscribe(const char *channel_name, void *pthis)
{
	CRedisSubscriber *p = (CRedisSubscriber *)pthis;
    int ret = redisAsyncCommand(p->_redis_context,
        &command_callback, p, "SUBSCRIBE %s", 
        channel_name);
    if (REDIS_ERR == ret)
    {
        //printf("Subscribe command failed: %d\n", ret);
		gf_log(p->conf->this->name, GF_LOG_ERROR, "jy_message: sub : Subscribe command failed: %d", ret);
        return _gf_false;
    }
    
    //printf(": Subscribe success: %s\n", channel_name);
	gf_log(p->conf->this->name, GF_LOG_ERROR, "jy_message: sub : Subscribe success: %s", channel_name);
    return _gf_true;
}

void sub_connect_callback(const redisAsyncContext *redis_context,
    int status)
{
    if (status != REDIS_OK)
    {
        printf(": Error: %s\n", redis_context->errstr);
    }
    else
    {
        printf(": Redis connected!\n");
    }
}

void sub_disconnect_callback(const redisAsyncContext *redis_context,
    int status)
{
    if (status != REDIS_OK)
    {
        printf(": Error: %s\n", redis_context->errstr);
    }
}

int get_next_tag(int idx, const char* const msg){
	if(idx < 0) return -1;
	
	while(msg[idx] != '\0' && msg[idx] != '^'){
		idx++;
	}
	if(msg[idx] == '\0' ||msg[idx + 1] != '^' ){
		return -1;
	}
	idx += 2;
	return idx;
}

double get_tag_value(const char* const msg){
	if(msg == NULL || msg[0] == '\0' || msg[0] < '0' || msg[0] > '9') return 0;
	int idx = 0;
	idx = get_next_tag(idx, msg);
	
	if(idx < 0) return atof(msg);
	idx -= 2;
	
	char d[30];
	int i = 0;
	for(i = 0; i < 30 && i < idx; i++){
		d[i] = msg[i];
	}
	if(i == 30) return 0;
	d[i] = '\0';
	return atof(d);
}

int get_nodename(const char* const msg, char* hostname){
	if(strlen(msg) == 0)
		return 0;
	int idx = 0;
	idx = get_next_tag(idx, msg);
	
	if(idx < 0){
		strcpy(hostname, msg);
		return 1;
	}

	idx -= 2;

	int i = 0;
	for(i = 0; i < 60 && i < idx; i++){
		hostname[i] = msg[i];
	}
	if(i == 60) return 0;
	hostname[i] = '\0';
	return 1;
}

void deal_reply(CRedisSubscriber *p, redisReply *redis_reply){
	/*
	// 订阅接收到的消息是一个带三元素的数组
	if (redis_reply->type == REDIS_REPLY_ARRAY &&
	redis_reply->elements == 3)
	{
		printf(": Recieve message:%s:%d:%s:%d:%s:%d\n",
		redis_reply->element[0]->str, redis_reply->element[0]->len,
		redis_reply->element[1]->str, redis_reply->element[1]->len,
		redis_reply->element[2]->str, redis_reply->element[2]->len);
				
		// 调用函数对象把消息通知给外层
		self_this->_notify_message_fn(redis_reply->element[1]->str,
			redis_reply->element[2]->str, redis_reply->element[2]->len);
	}
	*/
	
	if (redis_reply->type == REDIS_REPLY_ARRAY && redis_reply->elements == 3){
		




		int i = 0;
		int idx = 0;
		double v_wbw = -1;
		double v_rbw = -1;
		double v_diops = -1;
		int tag_count = MAX_TAG_NUM;
		for(i = 0; i < redis_reply->elements; i++){
			gf_log(p->conf->this->name, GF_LOG_ERROR, "jy_message: sub: NO.%d  message: %s", i, redis_reply->element[i]->str);
		}

		if(strncmp(redis_reply->element[0]->str,  "message", strlen( "message")) != 0){
			return;
		}
		
		gf_log(p->conf->this->name, GF_LOG_ERROR, "jy_message: sub:deal message");

		//增加删除app的处理
		if(redis_reply->element[2]->str == NULL) return;
		if(strncmp(redis_reply->element[2]->str, "remove_app", strlen("remove_app")) == 0){
			while(1){
				idx = get_next_tag(idx, redis_reply->element[2]->str);
				if(idx == -1)
					break;
				char hostname[60];
				char *msg = redis_reply->element[2]->str + idx;
				if(get_nodename(msg, hostname)){
					gf_log(p->conf->this->name, GF_LOG_ERROR, "jy_message: delete app:%s ", hostname);
					iot_qos_app_delete(p->conf, hostname);
				}
				
			}
			return;
			
		}

		//处理qos信息
		while(tag_count > 0){
			tag_count--;
			
			if(redis_reply->element[2]->str == NULL) return;
		
			//int idx = 0;
			//char tag[MAX_TAG_IDENTIFY];
			TAG_TYPE tag_t = APP_UN_DEFINE;

			idx = get_next_tag(idx, redis_reply->element[2]->str);
			if(idx == -1){
				gf_log(p->conf->this->name, GF_LOG_ERROR, "jy_message: sub_get_next_tag error ");
				break;
			}
			char *msg = redis_reply->element[2]->str + idx;

			//get tag
			if(strncmp(msg, "app_wbw", strlen( "app_wbw")) == 0){
				tag_t = APP_WBW;
				//gf_log(p->conf->this->name, GF_LOG_ERROR, "jy_message: sub_get_tag: app_wbw ");
			}
			else if(strncmp(msg, "app_rbw", strlen( "app_rbw")) == 0){
				//
				tag_t = APP_RBW;
				//return;
			}
			else if(strncmp(msg, "app_diops", strlen( "app_diops")) == 0){
				//
				tag_t = APP_DIOPS;
				//return;
			}
			else{
				//no match
				gf_log(p->conf->this->name, GF_LOG_ERROR, "jy_message: sub:no match tag");
				break;
			}

			//get value
			idx = get_next_tag(idx, redis_reply->element[2]->str);
			if(idx == -1){
				gf_log(p->conf->this->name, GF_LOG_ERROR, "jy_message: sub_get_next_tag error ");
				break;
			}
			msg = redis_reply->element[2]->str + idx;

			double val = get_tag_value(msg);
			//gf_log(p->conf->this->name, GF_LOG_ERROR, "jy_message: sub: get val: %f ", val);
			if(val <= 0){
				gf_log(p->conf->this->name, GF_LOG_ERROR, "jy_message: sub: invalid tag value");
				continue;
			}

			switch(tag_t){
				case APP_WBW:
					v_wbw = val;
					break;
				case APP_RBW:
					//to be realized
					v_rbw = val;
					break;
				case APP_DIOPS:
					v_diops = val;
					//to be realized
					break;
				case APP_UN_DEFINE:
					gf_log(p->conf->this->name, GF_LOG_ERROR, "jy_message: sub: TAG undefine");
					return;
			}
			
		}
		gf_log(p->conf->this->name, GF_LOG_ERROR, "jy_message: sub: get APP_WBW:%f, APP_RBW:%f, APP_DIOPS:%f", v_wbw, v_rbw, v_diops);
		set_app_bw(p->conf, redis_reply->element[2]->str, v_wbw, v_rbw, v_diops);
		
	}
}

void command_callback(redisAsyncContext *redis_context,
    void *reply, void *privdata)
{
	if (reply == NULL || privdata == NULL) {
		return ;
	}
	

	CRedisSubscriber *p = (CRedisSubscriber *) privdata;
	redisReply *redis_reply = (redisReply *) reply;

	gf_log(p->conf->this->name, GF_LOG_ERROR, "jy_message: sub: get command_callback message. reply_type:%d", redis_reply->type);

	deal_reply(p, redis_reply);
}

void *sub_event_thread(void *data)
{
    if (NULL == data)
    {
        printf(": Error!\n");\
        return NULL;
    }

    return sub_event_proc(data);
}

void *sub_event_proc(void *pthis)
{
    CRedisSubscriber *p = (CRedisSubscriber *)pthis;
    sem_wait(&p->_event_sem);

    event_base_dispatch(p->_event_base);

    return NULL;
}

void *sub_worker(iot_conf_t* conf){
	CRedisSubscriber *p;
	gf_log(conf->this->name, GF_LOG_ERROR, "jy_message: sub_worker");

	p = CALLOC (sizeof (*p), 1);
	p->conf = conf;
	conf->sub = p;
	
	int ret = sub_redis_init(p, conf);
	if (!ret)
	{
		p->conf->sub = NULL;
		printf("Init failed.\n");
		return NULL;
	}
	 
	ret = sub_redis_connect(p);
	if (!ret)
	{
		p->conf->sub = NULL;
		printf("Connect failed.\n");
		return NULL;
	}
	gf_log(conf->this->name, GF_LOG_ERROR, "jy_message: sub : connect ok");
	 
	subscribe("jy-test", p);
	/*
	p->state = IOT_STATE_ACTIVE;
	while (p->state == IOT_STATE_ACTIVE)
	{
		sleep(1);
	}

	p->conf->sub = NULL;
	sub_redis_disconnect(p);
	sub_redis_uninit(p);
	free(p);
	*/
	return p;

}

void sub_exit(iot_conf_t * conf){

	CRedisSubscriber *p = (CRedisSubscriber *)conf->sub;
	if(p && p->state == IOT_STATE_EXIT_REQUEST){

		conf->sub = NULL;
		sub_redis_disconnect(p);
		sub_redis_uninit(p);
		free(p);
	}
}

