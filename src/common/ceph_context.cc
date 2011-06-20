// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2011 New Dream Network
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include "common/DoutStreambuf.h"
#include "common/ProfLogger.h"
#include "common/Thread.h"
#include "common/ceph_context.h"
#include "common/config.h"
#include "common/debug.h"

#include <iostream>
#include <pthread.h>
#include <semaphore.h>

class CephContextServiceThread : public Thread
{
public:
  CephContextServiceThread(CephContext *cct)
    : _reopen_logs(false), _exit_thread(false), _cct(cct)
  {
    sem_init(&_sem, 0, 0);
  };

  ~CephContextServiceThread()
  {
    sem_destroy(&_sem);
  };

  void *entry()
  {
    while (1) {
      sem_wait(&_sem);
      if (_exit_thread) {
	break;
      }
      if (_reopen_logs) {
	_cct->_doss->reopen_logs(_cct->_conf);
	_cct->_prof_logger_collection->logger_reopen_all();
	_reopen_logs = false;
      }
    }
    return NULL;
  }

  void reopen_logs()
  {
    _reopen_logs = true;
    sem_post(&_sem);
  }

  void exit_thread()
  {
    _exit_thread = true;
    sem_post(&_sem);
  }

private:
  volatile bool _reopen_logs;
  volatile bool _exit_thread;
  sem_t _sem;
  CephContext *_cct;
};

CephContext::
CephContext(uint32_t module_type_)
  : _conf(new md_config_t()),
    _doss(new DoutStreambuf <char, std::basic_string<char>::traits_type>()),
    _dout(_doss),
    _module_type(module_type_),
    _service_thread(NULL),
    _prof_logger_collection(NULL),
    _prof_logger_conf_obs(NULL)
{
  pthread_spin_init(&_service_thread_lock, PTHREAD_PROCESS_SHARED);
  _prof_logger_collection = new ProfLoggerCollection(this);
  _prof_logger_conf_obs = new ProfLoggerConfObs(_prof_logger_collection);
  _conf->add_observer(_doss);
  _conf->add_observer(_prof_logger_conf_obs);
}

CephContext::
~CephContext()
{
  join_service_thread();

  _conf->remove_observer(_prof_logger_conf_obs);
  _conf->remove_observer(_doss);

  delete _prof_logger_collection;
  _prof_logger_collection = NULL;

  delete _prof_logger_conf_obs;
  _prof_logger_conf_obs = NULL;

  delete _doss;
  _doss = NULL;

  delete _conf;
  pthread_spin_destroy(&_service_thread_lock);

}

void CephContext::
start_service_thread()
{
  pthread_spin_lock(&_service_thread_lock);
  if (_service_thread) {
    pthread_spin_unlock(&_service_thread_lock);
    return;
  }
  _service_thread = new CephContextServiceThread(this);
  _service_thread->create();
  pthread_spin_unlock(&_service_thread_lock);
}

void CephContext::
reopen_logs()
{
  pthread_spin_lock(&_service_thread_lock);
  if (_service_thread)
    _service_thread->reopen_logs();
  pthread_spin_unlock(&_service_thread_lock);
}

void CephContext::
dout_lock(DoutLocker *locker)
{
  pthread_mutex_t *lock = &_doss->lock;
  pthread_mutex_lock(lock);
  locker->lock = lock;
}

void CephContext::
dout_trylock(DoutLocker *locker)
{
  static const int MAX_DOUT_TRYLOCK_TRIES = 3;

  /* Try a few times to get the lock. If we can't seem to get it, just give up. */
  pthread_mutex_t *lock = &_doss->lock;
  for (int i = 0; i < MAX_DOUT_TRYLOCK_TRIES; ++i) {
    if (pthread_mutex_trylock(lock) == 0) {
      locker->lock = lock;
      return;
    }
    usleep(50000);
  }
}

void CephContext::
join_service_thread()
{
  pthread_spin_lock(&_service_thread_lock);
  CephContextServiceThread *thread = _service_thread;
  if (!thread) {
    pthread_spin_unlock(&_service_thread_lock);
    return;
  }
  _service_thread = NULL;
  pthread_spin_unlock(&_service_thread_lock);

  thread->exit_thread();
  thread->join();
  delete thread;
}

uint32_t CephContext::
get_module_type() const
{
  return _module_type;
}

void CephContext::
set_module_type(uint32_t module_type_)
{
  _module_type = module_type_;
}

ProfLoggerCollection *CephContext::
GetProfLoggerCollection()
{
  return _prof_logger_collection;
}
