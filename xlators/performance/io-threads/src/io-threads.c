/*
  Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "call-stub.h"
#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "io-threads.h"
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include "locking.h"
#include <string.h>
#include "qos.h"
#include "redis_subscriber.h"

//QoS
void
_iot_qos_queue_with_stub (iot_worker_t *worker, call_stub_t *stub);

void
_iot_queue_with_stub (iot_worker_t *worker, call_stub_t *stub);

void
_iot_qos_queue (iot_worker_t *worker, iot_request_t *req);




typedef void *(*iot_worker_fn)(void*);

void
iot_stop_worker (iot_worker_t *worker);

void
iot_stop_workers (iot_worker_t **workers, int start_idx, int count);

void
_iot_queue (iot_worker_t *worker, iot_request_t *req);

iot_request_t *
iot_init_request (iot_worker_t *conf, call_stub_t *stub);

int
iot_startup_workers (iot_worker_t **workers, int start_idx, int count,
                     iot_worker_fn workerfunc);

void *
iot_worker_unordered (void *arg);

void *
iot_worker_ordered (void *arg);

int
iot_startup_worker (iot_worker_t *worker, iot_worker_fn workerfunc);

void
iot_destroy_request (iot_worker_t *worker, iot_request_t * req);

void
iot_notify_worker (iot_worker_t *worker)
{
#ifndef HAVE_SPINLOCK
        pthread_cond_broadcast (&worker->notifier);
#else
        sem_post (&worker->notifier);
#endif

        return;
}

int
iot_notify_wait (iot_worker_t *worker, int idletime)
{
        struct timeval  tv;
        struct timespec ts = {0, };
        int             waitres = 0;

        gettimeofday (&tv, NULL);
        /* Slightly skew the idle time for threads so that, we dont
         * have all of them rushing to exit at the same time, if
         * they've been idle.
         */
        ts.tv_sec = skew_sec_idle_time (tv.tv_sec + idletime);

#ifndef HAVE_SPINLOCK
        waitres = pthread_cond_timedwait (&worker->notifier, &worker->qlock,
                                          &ts);
#else
        UNLOCK (&worker->qlock);
        errno = 0;
        waitres = sem_timedwait (&worker->notifier, &ts);
        LOCK (&worker->qlock);
        if (waitres < 0)
                waitres = errno;
#endif

        return waitres;
}

void
iot_notify_init (iot_worker_t *worker)
{
        if (worker == NULL)
                return;

        LOCK_INIT (&worker->qlock);

#ifndef HAVE_SPINLOCK
        pthread_cond_init (&worker->notifier, NULL);
#else
        sem_init (&worker->notifier, 0, 0);
#endif



        return;
}

/* I know this function modularizes things a bit too much,
 * but it is easier on the eyes to read this than see all that locking,
 * queueing, and thread firing in the same curly block, as was the
 * case before this function.
 */
int
iot_request_queue_and_thread_fire (iot_worker_t *worker,
                                   iot_worker_fn workerfunc, iot_request_t *req)
{
        int     ret = -1; 
        LOCK (&worker->qlock);
        {
                if (iot_worker_active (worker)) {
					//QoS
					//if(req->stub->app != NULL && worker->queue_size > 0)
					//	_iot_qos_queue (worker, req);
					//else
                    //    _iot_queue (worker, req);

						_iot_queue (worker, req);
                        ret = 0;
                }else {
                        ret = iot_startup_worker (worker, workerfunc);
                        if (ret < 0) {
                                goto unlock;
                        }
					//QoS
					//if(req->stub->app != NULL && worker->queue_size > 0)
					//	_iot_qos_queue (worker, req);
					//else
					//	_iot_queue (worker, req);
						
                        _iot_queue (worker, req);
                }
        }
unlock:
        UNLOCK (&worker->qlock);

		//Qos

        return ret;
}

//QoS
//use stub instead of req
int
iot_request_queue_and_thread_fire_with_stub (iot_worker_t *worker,
                                   iot_worker_fn workerfunc, call_stub_t *stub)
{
        int     ret = -1; 

        LOCK (&worker->qlock);
        {
                if (iot_worker_active (worker)) {
					//QoS
					if(stub->app != NULL && worker->queue_size > 0)
						_iot_qos_queue_with_stub (worker, stub);
					else
                        _iot_queue_with_stub (worker, stub);
                        ret = 0;
                }else {
                        ret = iot_startup_worker (worker, workerfunc);
                        if (ret < 0) {
                                goto unlock;
                        }
					//QoS
					if(stub->app != NULL && worker->queue_size > 0)
						_iot_qos_queue_with_stub (worker, stub);
					else
						_iot_queue_with_stub (worker, stub);
                }
        }
unlock:
        UNLOCK (&worker->qlock);


        return ret;
}


int
iot_unordered_request_balancer (iot_conf_t *conf)
{
        long int        rand = 0;
        int             idx = 0;

        /* Decide which thread will service the request.
         * FIXME: This should change into some form of load-balancing.
         * */
        rand = random ();

        /* If scaling is on, we can choose from any thread
        * that has been allocated upto, max_o_threads, but
        * with scaling off, we'll never have threads more
        * than min_o_threads.
        */
        if (iot_unordered_scaling_on (conf))
                idx = (rand % conf->max_u_threads);
        else
                idx = (rand % conf->min_u_threads);

        return idx;
}


int
iot_schedule_unordered (iot_conf_t *conf, inode_t *inode, call_stub_t *stub)
{
        int32_t          idx = 0;
        iot_worker_t    *selected_worker = NULL;
        iot_request_t   *req = NULL;
        int             ret = -1;

        idx = iot_unordered_request_balancer (conf);
        selected_worker = conf->uworkers[idx];

        req = iot_init_request (selected_worker, stub);
        if (req == NULL) {
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_request_queue_and_thread_fire (selected_worker,
                                                 iot_worker_unordered, req);
        if (ret < 0) {
                iot_destroy_request (selected_worker, req);
        }
out:
        return ret;
}


/* Only to be used with ordered requests.
 */
uint64_t
iot_create_inode_worker_assoc (iot_conf_t * conf, inode_t * inode)
{
        long int        rand = 0;
        uint64_t        idx = 0;

        //rand = random ();
		//QoS
		rand = conf->rand;
		conf->rand++;
		
        /* If scaling is on, we can choose from any thread
        * that has been allocated upto, max_o_threads, but
        * with scaling off, we'll never have threads more
        * than min_o_threads.
        */
        if (iot_ordered_scaling_on (conf))
                idx = (rand % conf->max_o_threads);
        else
                idx = (rand % conf->min_o_threads);

        __inode_ctx_put (inode, conf->this, idx);

        return idx;
}


/* Assumes inode lock is held. */
int32_t
iot_ordered_request_balancer (iot_conf_t *conf, inode_t *inode, uint64_t *idx)
{
        int ret = -1;

        if (__inode_ctx_get (inode, conf->this, idx) < 0)
                *idx = iot_create_inode_worker_assoc (conf, inode);
        else {
                /* Sanity check to ensure the idx received from the inode
                * context is within bounds. We're a bit optimistic in
                * assuming that if an index is within bounds, it is
                * not corrupted. idx is uint so we dont check for less
                * than 0.
                */
                if ((*idx >= (uint64_t)conf->max_o_threads)) {
                        gf_log (conf->this->name, GF_LOG_DEBUG,
                                "inode context returned insane thread index %"
                                PRIu64, *idx);
                        ret = -EINVAL;
                        goto out;
                }
        }
        ret = 0;
out:
        return ret;
}

/*
* Replace worker queue with global conf queue
*/
int
iot_schedule_ordered (iot_conf_t *conf, inode_t *inode, call_stub_t *stub)
{
    int ret = 0;
	if(stub->app == NULL)
		pthread_mutex_lock(&conf->otlock);

	{
		//QoS priority queue
		if(stub->reserve_flag == _gf_false && stub->app != NULL){
			APP_Qos_Info *app = (APP_Qos_Info *)stub->app;
			
			list_add_tail(&stub->list, &conf->priority_req);
			
			gf_log(conf->this->name, GF_LOG_ERROR, "jy_message:app %s  priority_enqueue", app->uuid);
		}
		else{
			list_add_tail(&stub->list, &conf->normal_req);
		}

		pthread_cond_broadcast (&conf->ot_notifier);
	}

	if(stub->app == NULL)
		pthread_mutex_unlock(&conf->otlock);
	
    return ret;
}



int
iot_lookup_cbk (call_frame_t *frame, void * cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno,
                inode_t *inode, struct stat *buf, dict_t *xattr,
                struct stat *postparent)
{
        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, inode, buf, xattr,
                             postparent);
        return 0;
}


int
iot_lookup_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    dict_t *xattr_req)
{
        STACK_WIND (frame, iot_lookup_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->lookup,
                    loc, xattr_req);
        return 0;
}


int
iot_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xattr_req)
{
        call_stub_t     *stub = NULL;
        int              ret = -1;

        stub = fop_lookup_stub (frame, iot_lookup_wrapper, loc, xattr_req);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR,
                        "cannot create lookup stub (out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_unordered ((iot_conf_t *)this->private, loc->inode,
                                      stub);

out:
        if (ret < 0) {
                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
                STACK_UNWIND_STRICT (lookup, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL);
        }

        return 0;
}


int
iot_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 struct stat *preop, struct stat *postop)
{
        STACK_UNWIND_STRICT (setattr, frame, op_ret, op_errno, preop, postop);
        return 0;
}


int
iot_setattr_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                     struct stat *stbuf, int32_t valid)
{
        STACK_WIND (frame, iot_setattr_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->setattr,
                    loc, stbuf, valid);
        return 0;
}


int
iot_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
             struct stat *stbuf, int32_t valid)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_setattr_stub (frame, iot_setattr_wrapper, loc, stbuf, valid);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "Cannot create setattr stub"
                        "(Out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_unordered ((iot_conf_t *)this->private,
                                      loc->inode, stub);

out:
        if (ret < 0) {
                if (stub != NULL) {
                        call_stub_destroy (stub);
                }

                STACK_UNWIND_STRICT (setattr, frame, -1, -ret, NULL, NULL);
        }

        return 0;
}


int
iot_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  struct stat *preop, struct stat *postop)
{
        STACK_UNWIND_STRICT (fsetattr, frame, op_ret, op_errno, preop, postop);
        return 0;
}


int
iot_fsetattr_wrapper (call_frame_t *frame, xlator_t *this,
                      fd_t *fd, struct stat *stbuf, int32_t valid)
{
        STACK_WIND (frame, iot_fsetattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsetattr, fd, stbuf, valid);
        return 0;
}


int
iot_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
              struct stat *stbuf, int32_t valid)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_fsetattr_stub (frame, iot_fsetattr_wrapper, fd, stbuf,
                                  valid);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create fsetattr stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_ordered ((iot_conf_t *)this->private, fd->inode,
                                    stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (fsetattr, frame, -1, -ret, NULL, NULL);
                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_access_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno)
{
        STACK_UNWIND_STRICT (access, frame, op_ret, op_errno);
        return 0;
}


int
iot_access_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    int32_t mask)
{
        STACK_WIND (frame, iot_access_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->access, loc, mask);
        return 0;
}


int
iot_access (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_access_stub (frame, iot_access_wrapper, loc, mask);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create access stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_unordered ((iot_conf_t *)this->private, loc->inode,
                                      stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (access, frame, -1, -ret);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, const char *path,
                  struct stat *stbuf)
{
        STACK_UNWIND_STRICT (readlink, frame, op_ret, op_errno, path, stbuf);
        return 0;
}


int
iot_readlink_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                      size_t size)
{
        STACK_WIND (frame, iot_readlink_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->readlink,
                    loc, size);
        return 0;
}


int
iot_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_readlink_stub (frame, iot_readlink_wrapper, loc, size);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create readlink stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_unordered ((iot_conf_t *)this->private, loc->inode,
                                      stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (readlink, frame, -1, -ret, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }

        return 0;
}


int
iot_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, inode_t *inode,
               struct stat *buf, struct stat *preparent,
               struct stat *postparent)
{
        STACK_UNWIND_STRICT (mknod, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
        return 0;
}


int
iot_mknod_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
                   dev_t rdev)
{
        STACK_WIND (frame, iot_mknod_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->mknod, loc, mode, rdev);
        return 0;
}


int
iot_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
           dev_t rdev)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_mknod_stub (frame, iot_mknod_wrapper, loc, mode, rdev);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create mknod stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_unordered ((iot_conf_t *)this->private, loc->inode,
                                      stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (mknod, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_mkdir_cbk (call_frame_t *frame, void * cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, inode_t *inode,
               struct stat *buf, struct stat *preparent,
               struct stat *postparent)
{
        STACK_UNWIND_STRICT (mkdir, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
        return 0;
}


int
iot_mkdir_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode)
{
        STACK_WIND (frame, iot_mkdir_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->mkdir, loc, mode);
        return 0;
}


int
iot_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_mkdir_stub (frame, iot_mkdir_wrapper, loc, mode);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create mkdir stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_unordered ((iot_conf_t *)this->private, loc->inode,
                                      stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (mkdir, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct stat *preparent,
               struct stat *postparent)
{
        STACK_UNWIND_STRICT (rmdir, frame, op_ret, op_errno, preparent,
                             postparent);
        return 0;
}


int
iot_rmdir_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        STACK_WIND (frame, iot_rmdir_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->rmdir, loc);
        return 0;
}


int
iot_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_rmdir_stub (frame, iot_rmdir_wrapper, loc);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create rmdir stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_unordered ((iot_conf_t *)this->private, loc->inode,
                                      stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (rmdir, frame, -1, -ret, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_symlink_cbk (call_frame_t *frame, void * cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, inode_t *inode,
                 struct stat *buf, struct stat *preparent,
                 struct stat *postparent)
{
        STACK_UNWIND_STRICT (symlink, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
        return 0;
}


int
iot_symlink_wrapper (call_frame_t *frame, xlator_t *this, const char *linkname,
                     loc_t *loc)
{
        STACK_WIND (frame, iot_symlink_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->symlink, linkname, loc);
        return 0;
}


int
iot_symlink (call_frame_t *frame, xlator_t *this, const char *linkname,
             loc_t *loc)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_symlink_stub (frame, iot_symlink_wrapper, linkname, loc);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create symlink stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_unordered ((iot_conf_t *)this->private, loc->inode,
                                      stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (symlink, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL);
                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }

        return 0;
}


int
iot_rename_cbk (call_frame_t *frame, void * cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct stat *buf,
                struct stat *preoldparent, struct stat *postoldparent,
                struct stat *prenewparent, struct stat *postnewparent)
{
        STACK_UNWIND_STRICT (rename, frame, op_ret, op_errno, buf, preoldparent,
                             postoldparent, prenewparent, postnewparent);
        return 0;
}


int
iot_rename_wrapper (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
                    loc_t *newloc)
{
        STACK_WIND (frame, iot_rename_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->rename, oldloc, newloc);
        return 0;
}


int
iot_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_rename_stub (frame, iot_rename_wrapper, oldloc, newloc);
        if (!stub) {
                gf_log (this->name, GF_LOG_DEBUG, "cannot create rename stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_unordered ((iot_conf_t *)this->private,
                                      oldloc->inode, stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (rename, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL, NULL);
                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }

        return 0;
}


int
iot_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, fd_t *fd)
{
	STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd);
	return 0;
}


int
iot_open_wrapper (call_frame_t * frame, xlator_t * this, loc_t *loc,
                  int32_t flags, fd_t * fd, int32_t wbflags)
{
	STACK_WIND (frame, iot_open_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->open, loc, flags, fd, wbflags);
	return 0;
}


int
iot_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
          fd_t *fd, int32_t wbflags)
{
        call_stub_t	*stub = NULL;
        int             ret = -1;

        stub = fop_open_stub (frame, iot_open_wrapper, loc, flags, fd, wbflags);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR,
                        "cannot create open call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

	ret = iot_schedule_unordered ((iot_conf_t *)this->private, loc->inode,
                                      stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (open, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }

	return 0;
}


int
iot_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
                struct stat *stbuf, struct stat *preparent,
                struct stat *postparent)
{
	STACK_UNWIND_STRICT (create, frame, op_ret, op_errno, fd, inode, stbuf,
                             preparent, postparent);
	return 0;
}


int
iot_create_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    int32_t flags, mode_t mode, fd_t *fd)
{
	STACK_WIND (frame, iot_create_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->create,
		    loc, flags, mode, fd);
	return 0;
}


int
iot_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
            mode_t mode, fd_t *fd)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_create_stub (frame, iot_create_wrapper, loc, flags, mode,
                                fd);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR,
                        "cannot create \"create\" call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_unordered ((iot_conf_t *)this->private, loc->inode,
                                      stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (create, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }

        return 0;
}


int
iot_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iovec *vector,
               int32_t count, struct stat *stbuf, struct iobref *iobref)
{
	STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno, vector, count,
                             stbuf, iobref);

	return 0;
}


int
iot_readv_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                   off_t offset)
{
	STACK_WIND (frame, iot_readv_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->readv,
		    fd, size, offset);
	return 0;
}


int
iot_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
           off_t offset)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

	stub = fop_readv_stub (frame, iot_readv_wrapper, fd, size, offset);
	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR, 
			"cannot create readv call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
	}
        //QoS
    //call_stack_t *root = frame->root;
        client_id_t *client = (client_id_t*) frame->root->trans;
    //gf_log(this->name, GF_LOG_ERROR, "jy_message:size of req:%d, count:%d", vector[0].iov_len, count);
    //gf_log(this->name, GF_LOG_ERROR, "jy_message:req from client: %s", client->id);

	iot_conf_t *conf = (iot_conf_t *)this->private;
	APP_Qos_Info *app = NULL;
	int limit_flag = _gf_true;
	pthread_mutex_lock(&conf->otlock);
	{
		//get pp
		app = iot_qos_client_exist(client->id, conf);
		if(app == NULL){
			//gf_log(this->name, GF_LOG_ERROR, "jy_message:req from new client: %s", client->id);
			double reserve;
			double limit;
			int IS_IOPS = 0;
			double block_size = size;
			//for bandwidth, 100 means 100 MB/s;
			//for IOPS, 100 means 100 IO/s, block_size = X Byte
			//so bandwidth = IOPS * block_size / (1024 * 1024);
			/**
			reserve = conf->default_reserve;
			limit=conf->default_limit;
			**/
			
			if(conf->app_count % 5 == 0){
				//IS_IOPS = 0;
				reserve = 30;
				limit = 1000;
			}
			else{
				//IS_IOPS = 0;
				reserve = 30;
				limit = 1000;
			}
			if(IS_IOPS){
				
				reserve = reserve * (block_size / (1024 * 1024));
				limit = limit * (block_size / (1024 * 1024));
			}
			
			app = iot_qos_app_info_insert(conf, reserve, limit, client->id, IS_IOPS, block_size);
			if(app == NULL){
				gf_log(this->name, GF_LOG_ERROR, "jy_message:iot_qos_app_info_insert error!");
				pthread_mutex_unlock(&conf->otlock);
				ret = iot_schedule_ordered ((iot_conf_t *)this->private, fd->inode, stub);
				goto out;
			}
			else{
				stub->app = app;
                                //add_read
				stub->limit_time = app->read_last_time;
				pthread_mutex_lock(&app->mutex);
				{
					app->req_count++;
					app->queue_size++;
					iot_qos_set_next_reserve(app, stub);
				}
				pthread_mutex_unlock(&app->mutex);
			}
		}
		else {
			if(app->state == APP_SLEEP){
				list_del_init(&app->apps);
				app->state = APP_ACTIVE;
				list_add(&app->apps, &conf->apps);
				iot_qos_app_reweight(conf);
			}
		
			stub->app = app;
			pthread_mutex_lock(&app->mutex);
			{
				app->queue_size++;
				app->req_count++;
				limit_flag = iot_qos_set_next_limit(app, stub);
				stub->reserve_flag = iot_qos_set_next_reserve(app, stub);
			}
			pthread_mutex_unlock(&app->mutex);
		}
	

		if(limit_flag == _gf_false){
			//gf_log(this->name, GF_LOG_ERROR, "jy_message:app %s reach limit", app->uuid);
			ret = 0;
			//iot_qos_notify_limit_wait(conf, stub->limit_time);
			pthread_mutex_unlock(&conf->otlock);
			pthread_mutex_lock(&conf->lworker->qlock);
			{
				if(list_empty(&conf->lworker->rqlist)){
					list_add_tail(&stub->list, &conf->lworker->rqlist);
					pthread_cond_broadcast (&conf->lworker->notifier);
				}
				else{
					list_add_tail(&stub->list, &conf->lworker->rqlist);
				}
			}
			pthread_mutex_unlock(&conf->lworker->qlock);
			goto out;
		}
		else{
        	ret = iot_schedule_ordered ((iot_conf_t *)this->private, fd->inode,stub);
		}

	}
	pthread_mutex_unlock(&conf->otlock);
        //ret = iot_schedule_ordered ((iot_conf_t *)this->private, fd->inode,
        //                            stub);

out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (readv, frame, -1, -ret, NULL, -1, NULL,
                                     NULL);
                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
	return 0;
}


int
iot_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno)
{
	STACK_UNWIND_STRICT (flush, frame, op_ret, op_errno);
	return 0;
}


int
iot_flush_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
	STACK_WIND (frame, iot_flush_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->flush,
		    fd);
	return 0;
}


int
iot_flush (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

	stub = fop_flush_stub (frame, iot_flush_wrapper, fd);
	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create flush_cbk call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
	}

        ret = iot_schedule_ordered ((iot_conf_t *)this->private, fd->inode,
                                    stub);
out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (flush, frame, -1, -ret);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
	return 0;
}


int
iot_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct stat *prebuf,
               struct stat *postbuf)
{
	STACK_UNWIND_STRICT (fsync, frame, op_ret, op_errno, prebuf, postbuf);
	return 0;
}


int
iot_fsync_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                   int32_t datasync)
{
	STACK_WIND (frame, iot_fsync_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->fsync,
		    fd, datasync);
	return 0;
}


int
iot_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t datasync)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

	stub = fop_fsync_stub (frame, iot_fsync_wrapper, fd, datasync);
	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create fsync_cbk call stub"
                        "(out of memory)");
                ret = -1;
                goto out;
	}

        ret = iot_schedule_ordered ((iot_conf_t *)this->private, fd->inode,
                                    stub);

out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (fsync, frame, -1, -ret, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
	return 0;
}


int
iot_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct stat *prebuf,
                struct stat *postbuf)
{
	STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf);
	return 0;
}


int
iot_writev_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                    struct iovec *vector, int32_t count,
                    off_t offset, struct iobref *iobref)
{
	STACK_WIND (frame, iot_writev_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->writev,
		    fd, vector, count, offset, iobref);
	return 0;
}


int
iot_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
            struct iovec *vector, int32_t count, off_t offset,
            struct iobref *iobref)
{
	call_stub_t *stub = NULL;
        int         ret = -1;
	
    stub = fop_writev_stub (frame, iot_writev_wrapper,
				fd, vector, count, offset, iobref);

	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create writev call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
	}

		

	//QoS
    //call_stack_t *root = frame->root;
    client_id_t *client = (client_id_t*) frame->root->trans;
    //gf_log(this->name, GF_LOG_ERROR, "jy_message:size of req:%d, count:%d", vector[0].iov_len, count);
    //gf_log(this->name, GF_LOG_ERROR, "jy_message:req from client: %s", client->id);

	iot_conf_t *conf = (iot_conf_t *)this->private;
	APP_Qos_Info *app = NULL;
	int limit_flag = _gf_true;
	pthread_mutex_lock(&conf->otlock);
	{
		//get pp
		app = iot_qos_client_exist(client->id, conf);
		if(app == NULL){
			//gf_log(this->name, GF_LOG_ERROR, "jy_message:req from new client: %s", client->id);
			double reserve;
			double limit;
			int IS_IOPS = 0;
			double block_size = vector[0].iov_len * count;
			//for bandwidth, 100 means 100 MB/s;
			//for IOPS, 100 means 100 IO/s, block_size = X Byte
			//so bandwidth = IOPS * block_size / (1024 * 1024);
			/**
			reserve = conf->default_reserve;
			limit=conf->default_limit;
			**/
			
			if(conf->app_count % 5 == 0){
				//IS_IOPS = 0;
				reserve = 30;
				limit = 1000;
			}
			else{
				//IS_IOPS = 0;
				reserve = 30;
				limit = 1000;
			}
			if(IS_IOPS){
				
				reserve = reserve * (block_size / (1024 * 1024));
				limit = limit * (block_size / (1024 * 1024));
			}
			
			app = iot_qos_app_info_insert(conf, reserve, limit, client->id, IS_IOPS, block_size);
			if(app == NULL){
				gf_log(this->name, GF_LOG_ERROR, "jy_message:iot_qos_app_info_insert error!");
				pthread_mutex_unlock(&conf->otlock);
				ret = iot_schedule_ordered ((iot_conf_t *)this->private, fd->inode, stub);
				goto out;
			}
			else{
				stub->app = app;
                                //add_read
                                stub->limit_time = app->write_last_time;
                                
				pthread_mutex_lock(&app->mutex);
				{
					app->req_count++;
					app->queue_size++;
					iot_qos_set_next_reserve(app, stub);
				}
				pthread_mutex_unlock(&app->mutex);
			}
		}
		else {
			if(app->state == APP_SLEEP){
				list_del_init(&app->apps);
				app->state = APP_ACTIVE;
				list_add(&app->apps, &conf->apps);
				iot_qos_app_reweight(conf);
			}
		
			stub->app = app;
			pthread_mutex_lock(&app->mutex);
			{
				app->queue_size++;
				app->req_count++;
				limit_flag = iot_qos_set_next_limit(app, stub);
				stub->reserve_flag = iot_qos_set_next_reserve(app, stub);
			}
			pthread_mutex_unlock(&app->mutex);
		}
	

		if(limit_flag == _gf_false){
			//gf_log(this->name, GF_LOG_ERROR, "jy_message:app %s reach limit", app->uuid);
			ret = 0;
			//iot_qos_notify_limit_wait(conf, stub->limit_time);
			pthread_mutex_unlock(&conf->otlock);
			pthread_mutex_lock(&conf->lworker->qlock);
			{
				if(list_empty(&conf->lworker->rqlist)){
					list_add_tail(&stub->list, &conf->lworker->rqlist);
					pthread_cond_broadcast (&conf->lworker->notifier);
				}
				else{
					list_add_tail(&stub->list, &conf->lworker->rqlist);
				}
			}
			pthread_mutex_unlock(&conf->lworker->qlock);
			goto out;
		}
		else{
        	ret = iot_schedule_ordered ((iot_conf_t *)this->private, fd->inode,stub);
		}

	}
	pthread_mutex_unlock(&conf->otlock);
									
out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (writev, frame, -1, -ret, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }

	return 0;
}


int32_t
iot_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
            int32_t op_ret, int32_t op_errno, struct flock *flock)
{
	STACK_UNWIND_STRICT (lk, frame, op_ret, op_errno, flock);
	return 0;
}


int
iot_lk_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                int32_t cmd, struct flock *flock)
{
	STACK_WIND (frame, iot_lk_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->lk,
		    fd, cmd, flock);
	return 0;
}


int
iot_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
	struct flock *flock)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

	stub = fop_lk_stub (frame, iot_lk_wrapper, fd, cmd, flock);

	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create fop_lk call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
	}

        ret = iot_schedule_ordered ((iot_conf_t *)this->private, fd->inode,
                                    stub);
out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (lk, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
	return 0;
}


int
iot_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, struct stat *buf)
{
	STACK_UNWIND_STRICT (stat, frame, op_ret, op_errno, buf);
	return 0;
}


int
iot_stat_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	STACK_WIND (frame, iot_stat_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->stat,
		    loc);
	return 0;
}


int
iot_stat (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	call_stub_t *stub = NULL;
	fd_t        *fd = NULL;
        int         ret = -1;

        stub = fop_stat_stub (frame, iot_stat_wrapper, loc);
	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create fop_stat call stub"
                        "(out of memory)");
                ret = -1;
                goto out;
	}

	fd = fd_lookup (loc->inode, frame->root->pid);
        /* File is not open, so we can send it through unordered pool.
         */
	if (fd == NULL)
                ret = iot_schedule_unordered ((iot_conf_t *)this->private,
                                              loc->inode, stub);
        else {
                ret = iot_schedule_ordered ((iot_conf_t *)this->private,
                                            loc->inode, stub);
	        fd_unref (fd);
        }

out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (stat, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
	return 0;
}


int
iot_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct stat *buf)
{
	STACK_UNWIND_STRICT (fstat, frame, op_ret, op_errno, buf);
	return 0;
}


int
iot_fstat_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
	STACK_WIND (frame, iot_fstat_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fstat,
		    fd);
	return 0;
}


int
iot_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

	stub = fop_fstat_stub (frame, iot_fstat_wrapper, fd);
	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create fop_fstat call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
	}

        ret = iot_schedule_ordered ((iot_conf_t *)this->private, fd->inode,
                                    stub);
out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (fstat, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
	return 0;
}


int
iot_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct stat *prebuf,
                  struct stat *postbuf)
{
	STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno, prebuf,
                             postbuf);
	return 0;
}


int
iot_truncate_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                      off_t offset)
{
	STACK_WIND (frame, iot_truncate_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->truncate,
		    loc, offset);
	return 0;
}


int
iot_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset)
{
	call_stub_t *stub;
	fd_t        *fd = NULL;
        int         ret = -1;

        stub = fop_truncate_stub (frame, iot_truncate_wrapper, loc, offset);

	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create fop_stat call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
	}

	fd = fd_lookup (loc->inode, frame->root->pid);
	if (fd == NULL)
                ret = iot_schedule_unordered ((iot_conf_t *)this->private,
                                              loc->inode, stub);
        else {
                ret = iot_schedule_ordered ((iot_conf_t *)this->private,
                                            loc->inode, stub);
	        fd_unref (fd);
        }

out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (truncate, frame, -1, -ret, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }

	return 0;
}


int
iot_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct stat *prebuf,
                   struct stat *postbuf)
{
	STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno, prebuf,
                             postbuf);
	return 0;
}


int
iot_ftruncate_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                       off_t offset)
{
	STACK_WIND (frame, iot_ftruncate_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->ftruncate,
		    fd, offset);
	return 0;
}


int
iot_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

	stub = fop_ftruncate_stub (frame, iot_ftruncate_wrapper, fd, offset);
	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create fop_ftruncate call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
	}
        ret = iot_schedule_ordered ((iot_conf_t *)this->private, fd->inode,
                                    stub);
out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (ftruncate, frame, -1, -ret, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
	return 0;
}


int
iot_checksum_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int32_t op_ret, int32_t op_errno, uint8_t *file_checksum,
                  uint8_t *dir_checksum)
{
	STACK_UNWIND_STRICT (checksum, frame, op_ret, op_errno, file_checksum,
                             dir_checksum);
	return 0;
}


int
iot_checksum_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                      int32_t flags)
{
	STACK_WIND (frame, iot_checksum_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->checksum,
		    loc, flags);
  
	return 0;
}


int
iot_checksum (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

	stub = fop_checksum_stub (frame, iot_checksum_wrapper, loc, flags);

	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create fop_checksum call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
	}
        ret = iot_schedule_unordered ((iot_conf_t *)this->private, loc->inode,
                                      stub);
out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (checksum, frame, -1, -ret, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
	return 0;
}


int
iot_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		int32_t op_ret, int32_t op_errno, struct stat *preparent,
                struct stat *postparent)
{
	STACK_UNWIND_STRICT (unlink, frame, op_ret, op_errno, preparent,
                             postparent);
	return 0;
}


int
iot_unlink_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	STACK_WIND (frame, iot_unlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->unlink,
		    loc);
  
	return 0;
}


int
iot_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

	stub = fop_unlink_stub (frame, iot_unlink_wrapper, loc);
	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create fop_unlink call stub"
                        "(out of memory)");
                ret = -1;
                goto out;
	}

        ret = iot_schedule_unordered((iot_conf_t *)this->private, loc->inode,
                                     stub);

out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (unlink, frame, -1, -ret, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }

	return 0;
}


int
iot_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, inode_t *inode,
              struct stat *buf, struct stat *preparent, struct stat *postparent)
{
        STACK_UNWIND_STRICT (link, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
        return 0;
}


int
iot_link_wrapper (call_frame_t *frame, xlator_t *this, loc_t *old, loc_t *new)
{
        STACK_WIND (frame, iot_link_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->link, old, new);

        return 0;
}


int
iot_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_link_stub (frame, iot_link_wrapper, oldloc, newloc);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create link stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_unordered ((iot_conf_t *)this->private,
                                      oldloc->inode, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (link, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        STACK_UNWIND_STRICT (opendir, frame, op_ret, op_errno, fd);
        return 0;
}


int
iot_opendir_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd)
{
        STACK_WIND (frame, iot_opendir_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->opendir, loc, fd);
        return 0;
}


int
iot_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd)
{
        call_stub_t     *stub  = NULL;
        int             ret = -1;

        stub = fop_opendir_stub (frame, iot_opendir_wrapper, loc, fd);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create opendir stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_unordered ((iot_conf_t *)this->private, loc->inode,
                                      stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (opendir, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno)
{
        STACK_UNWIND_STRICT (fsyncdir, frame, op_ret, op_errno);
        return 0;
}


int
iot_fsyncdir_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                      int datasync)
{
        STACK_WIND (frame, iot_fsyncdir_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsyncdir, fd, datasync);
        return 0;
}


int
iot_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd, int datasync)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_fsyncdir_stub (frame, iot_fsyncdir_wrapper, fd, datasync);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create fsyncdir stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_ordered ((iot_conf_t *)this->private, fd->inode,
                                    stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (fsyncdir, frame, -1, -ret);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct statvfs *buf)
{
        STACK_UNWIND_STRICT (statfs, frame, op_ret, op_errno, buf);
        return 0;
}


int
iot_statfs_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        STACK_WIND (frame, iot_statfs_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->statfs, loc);
        return 0;
}


int
iot_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        call_stub_t     *stub = NULL;
        int              ret = -1;

        stub = fop_statfs_stub (frame, iot_statfs_wrapper, loc);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create statfs stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_unordered ((iot_conf_t *)this->private, loc->inode,
                                      stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (statfs, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno)
{
        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno);
        return 0;
}


int
iot_setxattr_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                      dict_t *dict, int32_t flags)
{
        STACK_WIND (frame, iot_setxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->setxattr, loc, dict, flags);
        return 0;
}


int
iot_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
              int32_t flags)
{
        call_stub_t     *stub = NULL;
        fd_t            *fd = NULL;
        int              ret = -1;

        stub = fop_setxattr_stub (frame, iot_setxattr_wrapper, loc, dict,
                                  flags);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create setxattr stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        fd = fd_lookup (loc->inode, frame->root->pid);
        if (fd == NULL)
                ret = iot_schedule_unordered ((iot_conf_t *)this->private,
                                              loc->inode, stub);
        else {
                ret = iot_schedule_ordered ((iot_conf_t *)this->private,
                                            loc->inode, stub);
                fd_unref (fd);
        }

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (setxattr, frame, -1, -ret);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, dict);
        return 0;
}


int
iot_getxattr_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                      const char *name)
{
        STACK_WIND (frame, iot_getxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->getxattr, loc, name);
        return 0;
}


int
iot_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
              const char *name)
{
        call_stub_t     *stub = NULL;
        fd_t            *fd = NULL;
        int             ret = -1;

        stub = fop_getxattr_stub (frame, iot_getxattr_wrapper, loc, name);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create getxattr stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        fd = fd_lookup (loc->inode, frame->root->pid);
        if (!fd)
                ret = iot_schedule_unordered ((iot_conf_t *)this->private,
                                              loc->inode, stub);
        else {
                ret = iot_schedule_ordered ((iot_conf_t *)this->private,
                                            loc->inode, stub);
                fd_unref (fd);
        }

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (getxattr, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_fgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        STACK_UNWIND_STRICT (fgetxattr, frame, op_ret, op_errno, dict);
        return 0;
}


int
iot_fgetxattr_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                       const char *name)
{
        STACK_WIND (frame, iot_fgetxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fgetxattr, fd, name);
        return 0;
}


int
iot_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
               const char *name)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_fgetxattr_stub (frame, iot_fgetxattr_wrapper, fd, name);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create fgetxattr stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_ordered ((iot_conf_t *)this->private, fd->inode,
                                    stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (fgetxattr, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno)
{
        STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno);
        return 0;
}


int
iot_fsetxattr_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                       dict_t *dict, int32_t flags)
{
        STACK_WIND (frame, iot_fsetxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsetxattr, fd, dict, flags);
        return 0;
}


int
iot_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
               int32_t flags)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_fsetxattr_stub (frame, iot_fsetxattr_wrapper, fd, dict,
                                        flags);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create fsetxattr stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_ordered ((iot_conf_t *)this->private, fd->inode,
                                    stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (fsetxattr, frame, -1, -ret);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno)
{
        STACK_UNWIND_STRICT (removexattr, frame, op_ret, op_errno);
        return 0;
}


int
iot_removexattr_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                         const char *name)
{
        STACK_WIND (frame, iot_removexattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->removexattr, loc, name);
        return 0;
}


int
iot_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                 const char *name)
{
        call_stub_t     *stub = NULL;
        fd_t            *fd = NULL;
        int             ret = -1;

        stub = fop_removexattr_stub (frame, iot_removexattr_wrapper, loc,
                                     name);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR,"cannot get removexattr fop"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        fd = fd_lookup (loc->inode, frame->root->pid);
        if (!fd)
                ret = iot_schedule_unordered ((iot_conf_t *)this->private,
                                              loc->inode, stub);
        else {
                ret = iot_schedule_ordered ((iot_conf_t *)this->private,
                                            loc->inode, stub);
                fd_unref (fd);
        }

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (removexattr, frame, -1, -ret);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, gf_dirent_t *entries)
{
        STACK_UNWIND_STRICT (readdirp, frame, op_ret, op_errno, entries);
        return 0;
}


int
iot_readdirp_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                      size_t size, off_t offset)
{
        STACK_WIND (frame, iot_readdirp_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->readdirp, fd, size, offset);
        return 0;
}


int
iot_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
              off_t offset)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_readdirp_stub (frame, iot_readdirp_wrapper, fd, size,
                                  offset);
        if (!stub) {
                gf_log (this->private, GF_LOG_ERROR,"cannot get readdir stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_ordered ((iot_conf_t *)this->private, fd->inode,
                                    stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (readdirp, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, gf_dirent_t *entries)
{
        STACK_UNWIND_STRICT (readdir, frame, op_ret, op_errno, entries);
        return 0;
}


int
iot_readdir_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                     size_t size, off_t offset)
{
        STACK_WIND (frame, iot_readdir_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->readdir, fd, size, offset);
        return 0;
}


int
iot_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t offset)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_readdir_stub (frame, iot_readdir_wrapper, fd, size, offset);
        if (!stub) {
                gf_log (this->private, GF_LOG_ERROR,"cannot get readdir stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_ordered ((iot_conf_t *)this->private, fd->inode,
                                    stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (readdir, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xattr)
{
        STACK_UNWIND_STRICT (xattrop, frame, op_ret, op_errno, xattr);
        return 0;
}


int
iot_xattrop_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                     gf_xattrop_flags_t optype, dict_t *xattr)
{
        STACK_WIND (frame, iot_xattrop_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->xattrop, loc, optype, xattr);
        return 0;
}


int
iot_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
             gf_xattrop_flags_t optype, dict_t *xattr)
{
        call_stub_t     *stub = NULL;
        fd_t            *fd = NULL;
        int             ret = -1;

        stub = fop_xattrop_stub (frame, iot_xattrop_wrapper, loc, optype,
                                        xattr);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create xattrop stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        fd = fd_lookup (loc->inode, frame->root->pid);
        if (!fd)
                ret = iot_schedule_unordered ((iot_conf_t *)this->private,
                                              loc->inode, stub);
        else {
                ret = iot_schedule_ordered ((iot_conf_t *)this->private,
                                            loc->inode, stub);
                fd_unref (fd);
        }

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (xattrop, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_fxattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xattr)
{
        STACK_UNWIND_STRICT (fxattrop, frame, op_ret, op_errno, xattr);
        return 0;
}

int
iot_fxattrop_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                      gf_xattrop_flags_t optype, dict_t *xattr)
{
        STACK_WIND (frame, iot_fxattrop_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fxattrop, fd, optype, xattr);
        return 0;
}

int
iot_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
              gf_xattrop_flags_t optype, dict_t *xattr)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_fxattrop_stub (frame, iot_fxattrop_wrapper, fd, optype,
                                        xattr);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create fxattrop stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_ordered ((iot_conf_t *)this->private, fd->inode,
                                    stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (fxattrop, frame, -1, -ret, NULL);
                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


/* Must be called with worker lock held */
void
_iot_queue (iot_worker_t *worker, iot_request_t *req)
{
        list_add_tail (&req->list, &worker->rqlist);

        /* dq_cond */
        worker->queue_size++;
        iot_notify_worker(worker);
}

//QoS
//because of iot_notify_worker(worker), we can only put it here
void
_iot_qos_queue (iot_worker_t *worker, iot_request_t *req){
	int flag = _gf_true;
	struct timeval tv = req->stub->limit_time;
	iot_request_t *tmp_req;
	list_for_each_back_entry (tmp_req, &worker->rqlist, list){
		if(tmp_req->stub->app) continue;
		struct timeval tmptv = tmp_req->stub->limit_time;
		double difftime = (tv.tv_sec - tmptv.tv_sec) * 1000000.0 + tv.tv_usec - tmptv.tv_usec;
		if(difftime > 0) {
			flag = _gf_false;
			break;
		}
	}
	
	if(flag == _gf_true)
		list_add_tail (&req->list, &worker->rqlist);
	else{
		list_add (&req->list, &tmp_req->list);
	}
	
	/* dq_cond */
	worker->queue_size++;
	iot_notify_worker(worker);
}

//use stub instead of req
void
_iot_queue_with_stub (iot_worker_t *worker, call_stub_t *stub)
{
        list_add_tail (&stub->list, &worker->rqlist);

        /* dq_cond */
        worker->queue_size++;
        iot_notify_worker(worker);
}

//use stub instead of req
void
_iot_qos_queue_with_stub (iot_worker_t *worker, call_stub_t *stub){


	
	int flag = _gf_true;
	struct timeval tv = stub->reserve_time;
	call_stub_t *tmp_stub;
	
	list_for_each_back_entry (tmp_stub, &worker->rqlist, list){
		if(tmp_stub->app == NULL) continue;
		struct timeval now = tmp_stub->limit_time;
		double difftime = (tv.tv_sec - now.tv_sec) * 1000000.0 + tv.tv_usec - now.tv_usec;
		if(difftime > 0) {
			flag = _gf_false;
			break;
		}
	}
	
	if(flag == _gf_true)
		list_add (&stub->list, &worker->rqlist);
	else{
		list_add (&stub->list, &tmp_stub->list);
	}

	
	/* dq_cond */
	worker->queue_size++;
	//gf_log(conf->this->name, GF_LOG_ERROR, "jy_message: worker %d enque. queue_size:%d", worker->thread_idx, worker->queue_size);
	iot_notify_worker(worker);
}
//end QoS



iot_request_t *
iot_init_request (iot_worker_t *worker, call_stub_t *stub)
{
	iot_request_t   *req = NULL;

        req = mem_get (worker->req_pool);
        if (req == NULL) {
                goto out;
        }

        req->stub = stub;
out:
        return req;
}


void
iot_destroy_request (iot_worker_t *worker, iot_request_t * req)
{
        if ((req == NULL) || (worker == NULL))
                return;

        mem_put (worker->req_pool, req);
}


/* Must be called with worker lock held. */
gf_boolean_t
iot_can_ordered_exit (iot_worker_t * worker)
{
        gf_boolean_t     allow_exit = _gf_false;
        iot_conf_t      *conf = NULL;

        conf = worker->conf;
        /* We dont want this thread to exit if its index is
         * below the min thread count.
         */
        if (worker->thread_idx >= conf->min_o_threads)
                allow_exit = _gf_true;

        return allow_exit;
}

/* Must be called with worker lock held. */
gf_boolean_t
iot_ordered_exit (int cond_waitres, iot_worker_t *worker)
{
        gf_boolean_t     allow_exit = _gf_false;

        if (worker->state == IOT_STATE_EXIT_REQUEST) {
                allow_exit = _gf_true;
        } else if (cond_waitres == ETIMEDOUT) {
                allow_exit = iot_can_ordered_exit (worker);
        }

        if (allow_exit) {
                worker->state = IOT_STATE_DEAD;
                worker->thread = 0;
        }

        return allow_exit;
}


int
iot_qos_ordered_request_wait (iot_worker_t * worker)
{
        int             waitres = 0;
        int             retstat = 0;

        if (worker->state == IOT_STATE_EXIT_REQUEST) {
                retstat = -1;
                goto out;
        }

		struct timeval idletime;
		gettimeofday(&idletime,NULL);
		idletime.tv_sec += worker->conf->o_idle_time;
        waitres = iot_qos_notify_wait (worker->conf, idletime);
		
        LOCK (&worker->qlock);
        if (iot_ordered_exit (waitres, worker)) {
                retstat = -1;
        }
		UNLOCK (&worker->qlock);

out:
        return retstat;
}

int
iot_qos_lworker_wait (limit_worker *worker)
{
        int waitres = 0;
        int retstat = 0;

        if (worker->state == IOT_STATE_EXIT_REQUEST) {
                retstat = -1;
                goto out;
        }

		struct timeval idletime;
		gettimeofday(&idletime,NULL);
		idletime.tv_sec += worker->conf->o_idle_time;
        waitres = iot_qos_notify_limit_wait (worker, idletime);

		if (worker->state == IOT_STATE_EXIT_REQUEST) {
            worker->state = IOT_STATE_DEAD;
            worker->thread = 0;
			retstat = -1;
        }


out:
        return retstat;
}


int
iot_ordered_request_wait (iot_worker_t * worker)
{
        int             waitres = 0;
        int             retstat = 0;

        if (worker->state == IOT_STATE_EXIT_REQUEST) {
                retstat = -1;
                goto out;
        }

        waitres = iot_notify_wait (worker, worker->conf->o_idle_time);

        if (iot_ordered_exit (waitres, worker)) {
                retstat = -1;
        }

out:
        return retstat;
}



call_stub_t *
iot_dequeue_ordered (iot_worker_t *worker)
{
	call_stub_t     *stub = NULL;
	iot_request_t   *req = NULL;
        int              waitstat = 0;

	LOCK (&worker->qlock);
        {
                while (!worker->queue_size) {
                        waitstat = 0;
                        waitstat = iot_ordered_request_wait (worker);
                        /* We must've timed out and are now required to
                         * exit.
                         */
                        if (waitstat == -1)
                                goto out;
                }

                list_for_each_entry (req, &worker->rqlist, list)
                        break;
                list_del (&req->list);
                stub = req->stub;

                worker->queue_size--;

				//QoS
				//list_del_init(&stub->req);
				if(stub->app != NULL && (stub->fop == GF_FOP_WRITE || stub->fop == GF_FOP_READ)){
					if(iot_qos_is_over_limit_time(stub->limit_time) == _gf_false){
						//gf_log(worker->conf->this->name, GF_LOG_ERROR, "jy_message:worker %d over limit!", worker->thread_idx);
						//iot_qos_notify_wait(worker, stub->limit_time);
					}
				}
				
        }

out:
	UNLOCK (&worker->qlock);
	
        iot_destroy_request (worker, req);

	return stub;
}

//QoS
//use stub instead req
call_stub_t *
iot_dequeue_ordered_with_stub (iot_worker_t *worker)
{
	call_stub_t     *stub = NULL;
	//iot_request_t   *req = NULL;
    int              waitstat = 0;
	
	iot_conf_t *conf = worker->conf;
	struct timeval now;

	pthread_mutex_lock(&conf->otlock);
	//LOCK (&worker->qlock);
        {
loop:
                //while (!worker->queue_size) {
				while (list_empty(&conf->normal_req) && list_empty(&conf->priority_req ))
				{
                        waitstat = 0;
                        waitstat = iot_qos_ordered_request_wait (worker);
                        /* We must've timed out and are now required to
                         * exit.
                         */
                        if (waitstat == -1)
                                goto out;
				}
				
				//deque
				{
					if(!list_empty(&conf->priority_req)){
					//priority queue
										
						gf_log(conf->this->name, GF_LOG_ERROR, "jy_message: worker %d deal priority queue", worker->thread_idx);
						//stub = iot_qos_priority_dequeue_ordered (conf);
						list_for_each_entry (stub, &conf->priority_req, list)
       						break;

					}
					else if(!list_empty(&conf->normal_req)){
					//normal queue
						list_for_each_entry (stub, &conf->normal_req, list)
       						break;

					}
					else{
						goto loop;
					}
				}

                list_del_init(&stub->list);
				

				//QoS
				if(stub->app != NULL && (stub->fop == GF_FOP_WRITE || stub->fop == GF_FOP_READ)){
					//gf_log(conf->this->name, GF_LOG_ERROR, "jy_message: worker %d deque. left queue_size:%d", worker->thread_idx, worker->queue_size);

					APP_Qos_Info* app = (APP_Qos_Info*) stub->app;
					pthread_mutex_lock(&app->mutex);
    				{
						app->queue_size--;
					}
					pthread_mutex_unlock(&app->mutex);

				}
				
				//check app eviction and print app throughput
				gettimeofday(&now,NULL);
				if(now.tv_sec > conf->ctime.tv_sec){
					conf->ctime = now;
					iot_qos_eviction_and_print_app_throughput(conf);
				}

				
		}
	

out:
	//UNLOCK (&worker->qlock);
	pthread_mutex_unlock(&conf->otlock);

        //iot_destroy_request (worker, req);

	return stub;
}

void *
iot_worker_ordered (void *arg)
{
        iot_worker_t    *worker = arg;
        call_stub_t     *stub = NULL;

		//QoS
		//iot_conf_t      *conf = worker->conf;
		

	while (1) {
		
		if(stub == NULL){
			stub = iot_dequeue_ordered_with_stub(worker);
		}

                //.stub = iot_dequeue_ordered (worker);
                

		
                /* If stub is NULL, we must've timed out waiting for a
                 * request and have now been allowed to exit.
                 */
                if (!stub)
                        break;

		call_resume (stub);

		stub = NULL;
	}

        return NULL;
}

void*
iot_qos_lworker(void *arg){
	limit_worker *worker = arg;
	call_stub_t *stub = NULL;
	iot_conf_t *conf = worker->conf;
	int waitstat = 0;
		
	while (1) {
		pthread_mutex_lock(&worker->qlock);
		while (list_empty(&worker->rqlist))
		{
			waitstat = 0;
			waitstat = iot_qos_lworker_wait (worker);
			if (waitstat == -1)
				goto out;
		}

		list_for_each_entry (stub, &worker->rqlist, list)
			break;
		
		list_del_init(&stub->list);

		if(stub->app != NULL && (stub->fop == GF_FOP_WRITE || stub->fop == GF_FOP_READ)){
			if(iot_qos_is_over_limit_time(stub->limit_time) == _gf_false){
				//gf_log(worker->conf->this->name, GF_LOG_ERROR, "jy_message:worker %d over limit!", worker->thread_idx);
				iot_qos_notify_limit_wait(worker, stub->limit_time);
			}
		}
		pthread_mutex_unlock(&worker->qlock);
		if (!stub)
			break;
		pthread_mutex_lock(&conf->otlock);
		list_add_tail(&stub->list, &conf->normal_req);
		pthread_cond_broadcast (&conf->ot_notifier);
		pthread_mutex_unlock(&conf->otlock);
		//call_resume (stub);

		stub = NULL;
	}
out:
        return NULL;
}



/* Must be called with worker lock held. */
gf_boolean_t
iot_can_unordered_exit (iot_worker_t * worker)
{
        gf_boolean_t    allow_exit = _gf_false;
        iot_conf_t      *conf = NULL;

        conf = worker->conf;
        /* We dont want this thread to exit if its index is
         * below the min thread count.
         */
        if (worker->thread_idx >= conf->min_u_threads)
                allow_exit = _gf_true;

        return allow_exit;
}


/* Must be called with worker lock held. */
gf_boolean_t
iot_unordered_exit (int cond_waitres, iot_worker_t *worker)
{
        gf_boolean_t     allow_exit = _gf_false;

        if (worker->state == IOT_STATE_EXIT_REQUEST) {
                allow_exit = _gf_true;
        } else if (cond_waitres == ETIMEDOUT) {
                allow_exit = iot_can_unordered_exit (worker);
        }

        if (allow_exit) {
                worker->state = IOT_STATE_DEAD;
                worker->thread = 0;
        }

        return allow_exit;
}


int
iot_unordered_request_wait (iot_worker_t * worker)
{
        int             waitres = 0;
        int             retstat = 0;

        if (worker->state == IOT_STATE_EXIT_REQUEST) {
                retstat = -1;
                goto out;
        }

        waitres = iot_notify_wait (worker, worker->conf->u_idle_time);
        if (iot_unordered_exit (waitres, worker)) {
                retstat = -1;
        }

out:
        return retstat;
}


call_stub_t *
iot_dequeue_unordered (iot_worker_t *worker)
{
        call_stub_t     *stub= NULL;
        iot_request_t   *req = NULL;
        int              waitstat = 0;

	LOCK (&worker->qlock);
        {
                while (!worker->queue_size) {
                        waitstat = 0;
                        waitstat = iot_unordered_request_wait (worker);
                        /* If -1, request wait must've timed
                         * out.
                         */
                        if (waitstat == -1)
                                goto out;
                }

                list_for_each_entry (req, &worker->rqlist, list)
                        break;
                list_del (&req->list);
                stub = req->stub;

                worker->queue_size--;
        }
out:
	UNLOCK (&worker->qlock);
        iot_destroy_request (worker, req);

	return stub;
}


void *
iot_worker_unordered (void *arg)
{
        iot_worker_t    *worker = arg;
        call_stub_t     *stub = NULL;

	while (1) {

		stub = iot_dequeue_unordered (worker);
                /* If no request was received, we must've timed out,
                 * and can exit. */
                if (!stub)
                        break;

		call_resume (stub);
	}

        return NULL;
}


void
deallocate_worker_array (iot_worker_t **workers)
{
        FREE (workers);
}

void
deallocate_workers (iot_worker_t **workers,
                    int start_alloc_idx, int count)
{
        int     i;
        int     end_count;

        end_count = count + start_alloc_idx;
        for (i = start_alloc_idx; (i < end_count); i++) {
                if (workers[i] != NULL) {
                        mem_pool_destroy (workers[i]->req_pool);
                        FREE (workers[i]);
                        workers[i] = NULL;
                }
        }
        
}


iot_worker_t **
allocate_worker_array (int count)
{
        iot_worker_t    **warr = NULL;

        warr = CALLOC (count, sizeof (iot_worker_t *));

        return warr;
}


iot_worker_t *
allocate_worker (iot_conf_t * conf)
{
        iot_worker_t    *wrk = NULL;

        wrk = CALLOC (1, sizeof (iot_worker_t));
        if (wrk == NULL) {
                gf_log (conf->this->name, GF_LOG_ERROR, "out of memory");
                goto out;
        }

        wrk->req_pool = mem_pool_new (iot_request_t, IOT_REQUEST_MEMPOOL_SIZE);
        if (wrk->req_pool == NULL)
                goto free_wrk;

        INIT_LIST_HEAD (&wrk->rqlist);
        wrk->conf = conf;
        iot_notify_init (wrk);
        wrk->state = IOT_STATE_DEAD;

out:
        return wrk;

free_wrk:
        FREE (wrk);
        return NULL;
}


int
allocate_workers (iot_conf_t *conf, iot_worker_t **workers, int start_alloc_idx,
                  int count)
{
        int     i;
        int     end_count, ret = -1;

        end_count = count + start_alloc_idx;
        for (i = start_alloc_idx; i < end_count; i++) {
                workers[i] = allocate_worker (conf);
                if (workers[i] == NULL) {
                        ret = -ENOMEM;
                        goto out;
                }
                workers[i]->thread_idx = i;

			//QoS message
			gf_log(conf->this->name, GF_LOG_ERROR, "jy_message:allocate worker %d!",i);
        }

        ret = 0;

out:
        return ret;
}


void
iot_stop_worker (iot_worker_t *worker)
{
        LOCK (&worker->qlock);
        {
                worker->state = IOT_STATE_EXIT_REQUEST;
        }
        UNLOCK (&worker->qlock);

        iot_notify_worker (worker);
        pthread_join (worker->thread, NULL);
}


void
iot_stop_workers (iot_worker_t **workers, int start_idx, int count)
{
        int     i = 0;
        int     end_idx = 0;

        end_idx = start_idx + count;
        for (i = start_idx; i < end_idx; i++) {
                if (workers[i] != NULL) {
                        iot_stop_worker (workers[i]);
                }
        }
}


int
iot_startup_worker (iot_worker_t *worker, iot_worker_fn workerfunc)
{
        int     ret = -1;
        ret = pthread_create (&worker->thread, &worker->conf->w_attr,
                              workerfunc, worker);
        if (ret != 0) {
                gf_log (worker->conf->this->name, GF_LOG_ERROR,
                        "cannot start worker (%s)", strerror (errno));
                ret = -ret;
        } else {
                worker->state = IOT_STATE_ACTIVE;
        }

        return ret;
}


int
iot_startup_workers (iot_worker_t **workers, int start_idx, int count,
                     iot_worker_fn workerfunc)
{
        int     i = 0;
        int     end_idx = 0;
        int     ret = -1; 

        end_idx = start_idx + count;
        for (i = start_idx; i < end_idx; i++) {
                ret = iot_startup_worker (workers[i], workerfunc);
                if (ret < 0) {
                        goto out;
                }
        //Qos
        gf_log(workers[i]->conf->this->name, GF_LOG_ERROR, "jy_message:startup worker %d!", i);
        }

        ret = 0;
out:
        return ret;
}


void
set_stack_size (iot_conf_t *conf)
{
        int     err = 0;
        size_t  stacksize = IOT_THREAD_STACK_SIZE;

        pthread_attr_init (&conf->w_attr);
        err = pthread_attr_setstacksize (&conf->w_attr, stacksize);
        if (err == EINVAL) {
                gf_log (conf->this->name, GF_LOG_WARNING,
                                "Using default thread stack size");
        }
}

void
iot_cleanup_workers (iot_conf_t *conf)
{
		//QoS clean limit _worker
		pthread_mutex_lock (&conf->lworker->qlock);
        {
                conf->lworker->state = IOT_STATE_EXIT_REQUEST;
        }
        pthread_mutex_unlock (&conf->lworker->qlock);
        pthread_cond_broadcast (&conf->lworker->notifier);
        pthread_join (conf->lworker->thread, NULL);

		CRedisSubscriber *p = (CRedisSubscriber *)conf->sub;
		if(p != NULL)
			p->state = IOT_STATE_EXIT_REQUEST;
		sub_exit(conf);
		
		
        if (conf->uworkers != NULL) {
                iot_stop_workers (conf->uworkers, 0,
                                  conf->max_u_threads);
                    
                deallocate_workers (conf->uworkers, 0,
                                    conf->max_u_threads);

                deallocate_worker_array (conf->uworkers);
        }
                
        if (conf->oworkers != NULL) {
                iot_stop_workers (conf->oworkers, 0,
                                  conf->max_o_threads);
                        
                deallocate_workers (conf->oworkers, 0,
                                    conf->max_o_threads);
                        
                deallocate_worker_array (conf->oworkers);
        }
}

limit_worker*
iot_qos_init_limit_worker (iot_conf_t * conf)
{
        limit_worker* wrk = NULL;

        wrk = CALLOC (1, sizeof (limit_worker));
        if (wrk == NULL) {
                gf_log (conf->this->name, GF_LOG_ERROR, "out of memory");
                goto out;
        }

        INIT_LIST_HEAD (&wrk->rqlist);
        wrk->conf = conf;

		pthread_mutex_init (&wrk->qlock, NULL);
		pthread_cond_init (&wrk->notifier, NULL);
        wrk->state = IOT_STATE_DEAD;

		int ret = pthread_create (&wrk->thread, &conf->w_attr,
                              iot_qos_lworker, wrk);
        if (ret != 0) {
                gf_log (wrk->conf->this->name, GF_LOG_ERROR,
                        "cannot start worker (%s)", strerror (errno));
				goto free_wrk;
        } else {
                wrk->state = IOT_STATE_ACTIVE;
        }

out:
        return wrk;

free_wrk:
        FREE (wrk);
        return NULL;
}


int
workers_init (iot_conf_t *conf)
{
        int     ret = -1;

        if (conf == NULL) {
                ret = -EINVAL;
                goto err;
        }

		//QoS: init limit worker
		conf->lworker = iot_qos_init_limit_worker(conf);

        /* Initialize un-ordered workers */
        conf->uworkers = allocate_worker_array (conf->max_u_threads);
        if (conf->uworkers == NULL) {
                gf_log (conf->this->name, GF_LOG_ERROR, "out of memory");
                ret = -ENOMEM;
                goto err;
        }

        ret = allocate_workers (conf, conf->uworkers, 0,
                                conf->max_u_threads);
        if (ret < 0) {
                gf_log (conf->this->name, GF_LOG_ERROR, "out of memory");
                goto err;
        }

        /* Initialize ordered workers */
        conf->oworkers = allocate_worker_array (conf->max_o_threads);
        if (conf->oworkers == NULL) {
                gf_log (conf->this->name, GF_LOG_ERROR, "out of memory");
                ret = -ENOMEM;
                goto err;
        }

        ret = allocate_workers (conf, conf->oworkers, 0,
                                conf->max_o_threads);
        if (ret < 0) {
                gf_log (conf->this->name, GF_LOG_ERROR, "out of memory");
                goto err;
        }

        set_stack_size (conf);
        ret = iot_startup_workers (conf->oworkers, 0, conf->min_o_threads,
                                   iot_worker_ordered);
        if (ret == -1) {
                /* logged inside iot_startup_workers */
                goto err;
        }

        ret = iot_startup_workers (conf->uworkers, 0, conf->min_u_threads,
                                   iot_worker_unordered);
        if (ret == -1) {
                /* logged inside iot_startup_workers */
                goto err;
        }
    //Qos
    gf_log(conf->this->name, GF_LOG_ERROR, "jy_message:init %d workers over!", conf->thread_count);

        return 0;

err:
        if (conf != NULL)  {
                iot_cleanup_workers (conf);
        }

        return ret;
}


int
init (xlator_t *this)
{
        iot_conf_t      *conf = NULL;
        dict_t          *options = this->options;
        int             thread_count = IOT_DEFAULT_THREADS;
        gf_boolean_t    autoscaling = IOT_SCALING_OFF;
        char            *scalestr = NULL;
        int             min_threads, max_threads, ret = -1;
        
	if (!this->children || this->children->next) {
		gf_log ("io-threads", GF_LOG_ERROR,
			"FATAL: iot not configured with exactly one child");
                goto out;
	}

	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"dangling volume. check volfile ");
	}

	conf = (void *) CALLOC (1, sizeof (*conf));
        if (conf == NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory");
                goto out;
        }

        if ((dict_get_str (options, "autoscaling", &scalestr)) == 0) {
                if ((gf_string2boolean (scalestr, &autoscaling)) == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                        "'autoscaling' option must be"
                                        " boolean");
                        goto out;
                }
        }

	if (dict_get (options, "thread-count")) {
                thread_count = data_to_int32 (dict_get (options,
                                        "thread-count"));
                if (scalestr != NULL)
                        gf_log (this->name, GF_LOG_WARNING,
                                        "'thread-count' is specified with "
                                        "'autoscaling' on. Ignoring"
                                        "'thread-count' option.");
                if (thread_count < 2)
                        thread_count = IOT_MIN_THREADS;
        }

        min_threads = IOT_DEFAULT_THREADS;
        max_threads = IOT_MAX_THREADS;
        if (dict_get (options, "min-threads"))
                min_threads = data_to_int32 (dict_get (options,
                                        "min-threads"));

        if (dict_get (options, "max-threads"))
                max_threads = data_to_int32 (dict_get (options,
                                        "max-threads"));
       
        if (min_threads > max_threads) {
                gf_log (this->name, GF_LOG_ERROR, " min-threads must be less "
                                "than max-threads");
                goto out;
        }

        /* If autoscaling is off, then adjust the min and max
         * threads according to thread-count.
         * This is based on the assumption that despite autoscaling
         * being off, we still want to have separate pools for data
         * and meta-data threads.
         */
        if (!autoscaling)
                max_threads = min_threads = thread_count;

        /* If user specifies an odd number of threads, increase it by
         * one. The reason for having an even number of threads is
         * explained later.
         */
        if (max_threads % 2)
                max_threads++;

        if(min_threads % 2)
                min_threads++;

        /* If the user wants to have only a single thread for
        * some strange reason, make sure we set this count to
        * 2. Explained later.
        */
        if (min_threads < IOT_MIN_THREADS)
                min_threads = IOT_MIN_THREADS;

        /* Again, have atleast two. Read on. */
        if (max_threads < IOT_MIN_THREADS)
                max_threads = IOT_MIN_THREADS;

        /* This is why we need atleast two threads.
         * We're dividing the specified thread pool into
         * 2 halves, equally between ordered and unordered
         * pools.
         */

        /* Init params for un-ordered workers. */
        pthread_mutex_init (&conf->utlock, NULL);
        conf->max_u_threads = max_threads / 2;
        conf->min_u_threads = min_threads / 2;
        conf->u_idle_time = IOT_DEFAULT_IDLE;
        conf->u_scaling = autoscaling;

        /* Init params for ordered workers. */
        pthread_mutex_init (&conf->otlock, NULL);
        conf->max_o_threads = max_threads / 2;
        conf->min_o_threads = min_threads / 2;
        conf->o_idle_time = IOT_DEFAULT_IDLE;
        conf->o_scaling = autoscaling;

        gf_log (this->name, GF_LOG_DEBUG,
                "io-threads: Autoscaling: %s, "
                "min_threads: %d, max_threads: %d",
                (autoscaling) ? "on":"off", min_threads, max_threads);

        conf->this = this;

    //Qos
    INIT_LIST_HEAD(&conf->apps);
	INIT_LIST_HEAD(&conf->priority_req);
	INIT_LIST_HEAD(&conf->normal_req);
	pthread_cond_init (&conf->ot_notifier, NULL);
	pthread_cond_init (&conf->limit_notifier, NULL);
    conf->app_count = 0;
	conf->rand = 0;
	gettimeofday(&conf->ctime,NULL);
	//get QoS setting
	if (dict_get (options, "default-reserve"))
                conf->default_reserve = data_to_int32 (dict_get (options,
                                        "default-reserve"));
	else
		conf->default_reserve = DEFAULT_RESERVE;
	if (dict_get (options, "default-limit"))
                conf->default_limit = data_to_int32 (dict_get (options,
                                        "default-limit"));
	else
		conf->default_limit = DEFAULT_LIMIT;
	if (dict_get (options, "default-BW")){
                conf->default_BW = data_to_int32 (dict_get (options,
                                        "default-BW"));
				conf->default_BW *= 1024;	//converted to KB / s
		}
	else
		conf->default_BW = IOT_MAX_BANDWIDTH;

	char *redis_host = NULL;
	int redis_port = 0;
	if (dict_get (options, "redis-host"))
		redis_host = data_to_str (dict_get (options, "redis-host"));
	if (dict_get (options, "redis-port"))
		redis_port = data_to_int32 (dict_get (options, "redis-port"));

	if(redis_host){
		conf->redis_host = CALLOC (1, sizeof(redis_host));
		ERR_ABORT(conf->redis_host);
		strcpy(conf->redis_host, redis_host);
	}
	else
		conf->redis_host = NULL;
	conf->redis_port = redis_port;

	gf_log (this->name, GF_LOG_ERROR, "jy-setting:	reserve:%d	limit:%d,	BW:%d", conf->default_reserve, conf->default_limit, conf->default_BW);

		
	ret = workers_init (conf);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "cannot initialize worker threads, exiting init");
                FREE (conf);
                goto out;
        }
	
	conf->sub = NULL;
 	//int sub_ret = pthread_create (&conf->sub_thread, &conf->w_attr, sub_worker, conf);
    if (sub_worker(conf) == NULL) {
            gf_log (conf->this->name, GF_LOG_ERROR,
                    "cannot start worker (%s)", strerror (errno));
    }

	this->private = conf;
        ret = 0;
out:
	return ret;
}


void
fini (xlator_t *this)
{
	iot_conf_t *conf = this->private;

	//QoS free
	APP_Qos_Info *tmp_app;
	while(conf->app_count > 0){
		conf->app_count -= 1;
		list_for_each_entry (tmp_app, &conf->apps, apps)
			break;
		list_del(&tmp_app->apps);
		free(tmp_app);
	}
	list_del(&conf->apps);
	list_del(&conf->priority_req);
	list_del(&conf->normal_req);

	FREE (conf);

	this->private = NULL;
	return;
}

/*
 * O - Goes to ordered threadpool.
 * U - Goes to un-ordered threadpool.
 * V - Variable, depends on whether the file is open.
 *     If it is, then goes to ordered, otherwise to
 *     un-ordered.
 */
struct xlator_fops fops = {
	.open        = iot_open,        /* U */
	.create      = iot_create,      /* U */
	.readv       = iot_readv,       /* O */
	.writev      = iot_writev,      /* O */
	.flush       = iot_flush,       /* O */
	.fsync       = iot_fsync,       /* O */
	.lk          = iot_lk,          /* O */
	.stat        = iot_stat,        /* V */
	.fstat       = iot_fstat,       /* O */
	.truncate    = iot_truncate,    /* V */
	.ftruncate   = iot_ftruncate,   /* O */
	.checksum    = iot_checksum,    /* U */
	.unlink      = iot_unlink,      /* U */
        .lookup      = iot_lookup,      /* U */
        .setattr     = iot_setattr,     /* U */
        .fsetattr    = iot_fsetattr,    /* O */
        .access      = iot_access,      /* U */
        .readlink    = iot_readlink,    /* U */
        .mknod       = iot_mknod,       /* U */
        .mkdir       = iot_mkdir,       /* U */
        .rmdir       = iot_rmdir,       /* U */
        .symlink     = iot_symlink,     /* U */
        .rename      = iot_rename,      /* U */
        .link        = iot_link,        /* U */
        .opendir     = iot_opendir,     /* U */
        .fsyncdir    = iot_fsyncdir,    /* O */
        .statfs      = iot_statfs,      /* U */
        .setxattr    = iot_setxattr,     /* U */
        .getxattr    = iot_getxattr,    /* U */
        .fgetxattr   = iot_fgetxattr,   /* O */
        .fsetxattr   = iot_fsetxattr,   /* O */
        .removexattr = iot_removexattr, /* U */
        .readdir     = iot_readdir,     /* O */
        .readdirp    = iot_readdirp,    /* O */
        .xattrop     = iot_xattrop,     /* U */
	.fxattrop    = iot_fxattrop,    /* O */
};

struct xlator_mops mops = {
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
	{ .key  = {"thread-count"}, 
	  .type = GF_OPTION_TYPE_INT, 
	  .min  = IOT_MIN_THREADS, 
	  .max  = IOT_MAX_THREADS
	},
        { .key  = {"autoscaling"},
          .type = GF_OPTION_TYPE_BOOL
        },
        { .key          = {"min-threads"},
          .type         = GF_OPTION_TYPE_INT,
          .min          = IOT_MIN_THREADS,
          .max          = IOT_MAX_THREADS,
          .description  = "Minimum number of threads must be greater than or "
                          "equal to 2. If the specified value is less than 2 "
                          "it is adjusted upwards to 2. This is a requirement"
                          " for the current model of threading in io-threads."
        },
        { .key          = {"max-threads"},
          .type         = GF_OPTION_TYPE_INT,
          .min          = IOT_MIN_THREADS,
          .max          = IOT_MAX_THREADS,
          .description  = "Maximum number of threads is advisory only so the "
                          "user specified value will be used."
        },
        { .key  = {"default-reserve"},
          .type = GF_OPTION_TYPE_INT,
        },
        { .key  = {"default-limit"},
          .type = GF_OPTION_TYPE_INT,
        },
        { .key  = {"default-BW"},
          .type = GF_OPTION_TYPE_INT,
        },
        { .key  = {"redis-host"},
          .type = GF_OPTION_TYPE_STR,
        },
        { .key  = {"redis-port"},
          .type = GF_OPTION_TYPE_INT,
        },
	{ .key  = {NULL} },
};
