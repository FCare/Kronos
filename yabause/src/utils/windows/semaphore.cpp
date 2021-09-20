
/*
	Copyright (c) 2011, Dongsheng Song <songdongsheng@live.cn>

	Licensed to the Apache Software Foundation (ASF) under one or more
	contributor license agreements.  See the NOTICE file distributed with
	this work for additional information regarding copyright ownership.
	The ASF licenses this file to You under the Apache License, Version 2.0
	(the "License"); you may not use this file except in compliance with
	the License.  You may obtain a copy of the License at
	   http://www.apache.org/licenses/LICENSE-2.0
	Unless required by applicable law or agreed to in writing, software
	distributed under the License is distributed on an "AS IS" BASIS,
	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
	See the License for the specific language governing permissions and
	limitations under the License.
*/

/*
	Simple Windows replacement for POSIX semaphores
	Modified by Daniel Tillett from libpthread <http://github.com/songdongsheng/libpthread>
	Copyright (c) 2015, Daniel Tillett <daniel.tillett @ gmail.com>
*/

#include "semaphore.h"
#include <time.h> //c++11 required

typedef struct {
	HANDLE handle;
} arch_sem_t;

static int lc_set_errno(int result) {
	if (result != 0) {
		errno = result;
		return -1;
		}

	return 0;
	}

int sem_init(sem_t *sem, int pshared, unsigned int value)
{
	char buf[24] = {'\0'};
	arch_sem_t *pv;

	if (sem == NULL || value > (unsigned int) SEM_VALUE_MAX) 
	{
		return lc_set_errno(EINVAL);
	}

	if (NULL == (pv = (arch_sem_t *)calloc(1, sizeof(arch_sem_t)))) 
	{
		return lc_set_errno(ENOMEM);
	}

	if (pshared != PTHREAD_PROCESS_PRIVATE) {
		sprintf(buf, "Global\\%p", pv);
	}

	if ((pv->handle = CreateSemaphoreA(NULL, value, SEM_VALUE_MAX, buf)) == NULL) 
	{
		free(pv);
		return lc_set_errno(ENOSPC);
	}

	*sem = pv;
	return 0;
}

int sem_wait(sem_t *sem)
{
	arch_sem_t *pv = (arch_sem_t *) sem;

	if (sem == NULL || pv == NULL) 
	{
		return lc_set_errno(EINVAL);
	}

	if (WaitForSingleObject(pv->handle, INFINITE) != WAIT_OBJECT_0) 
	{
		return lc_set_errno(EINVAL);
	}

	return 0;
}

int sem_trywait(sem_t *sem)
{
	unsigned rc;
	arch_sem_t *pv = (arch_sem_t *) sem;

	if (sem == NULL || pv == NULL) 
	{
		return lc_set_errno(EINVAL);
	}

	if ((rc = WaitForSingleObject(pv->handle, 0)) == WAIT_OBJECT_0) 
	{
		return 0;
	}

	if (rc == WAIT_TIMEOUT) 
	{
		return lc_set_errno(EAGAIN);
	}

	return lc_set_errno(EINVAL);
}

#define INT64_MAX				0x7fffffffffffffff
#define INT64_C(x)				((x) + (INT64_MAX - INT64_MAX))
#define DELTA_EPOCH_IN_100NS    INT64_C(116444736000000000)
#define POW10_3					INT64_C(1000)
#define POW10_4					INT64_C(10000)
#define POW10_6					INT64_C(1000000)

static __int64 FileTimeToUnixTimeIn100NS(FILETIME *input)
{
	return (((__int64) input->dwHighDateTime) << 32 | input->dwLowDateTime) - DELTA_EPOCH_IN_100NS;
}

static __int64 arch_time_in_ms(void)
{
	FILETIME time;
	GetSystemTimeAsFileTime(&time);
	return FileTimeToUnixTimeIn100NS(&time) / POW10_4;
}

static  __int64 arch_time_in_ms_from_timespec(const struct timespec *ts)
{
	return ts->tv_sec * POW10_3 + ts->tv_nsec / POW10_6;
	}

static unsigned arch_rel_time_in_ms(const struct timespec *ts)
{
	__int64 t1 = arch_time_in_ms_from_timespec(ts);
	__int64 t2 = arch_time_in_ms();
	__int64 t = t1 - t2;

	if (t < 0 || t >= INT64_C(4294967295)) 
	{
		return 0;
	}

	return (unsigned) t;
}

int sem_timedwait(sem_t *sem, const struct timespec *abs_timeout)
{
	unsigned rc;
	arch_sem_t *pv = (arch_sem_t *) sem;

	if (sem == NULL || pv == NULL) 
	{
		return lc_set_errno(EINVAL);
	}

	if ((rc = WaitForSingleObject(pv->handle, arch_rel_time_in_ms(abs_timeout))) == WAIT_OBJECT_0) 
	{
		return 0;
	}

	if (rc == WAIT_TIMEOUT) 
	{
		return lc_set_errno(ETIMEDOUT);
	}

	return lc_set_errno(EINVAL);
}

int sem_post(sem_t *sem)
{
	arch_sem_t *pv = (arch_sem_t *) sem;

	if (sem == NULL || pv == NULL) 
	{
		return lc_set_errno(EINVAL);
	}

	if (ReleaseSemaphore(pv->handle, 1, NULL) == 0) 
	{
		return lc_set_errno(EINVAL);
	}

	return 0;
}

int sem_getvalue(sem_t *sem, int *value)
{
	long previous;
	arch_sem_t *pv = (arch_sem_t *) sem;

	switch (WaitForSingleObject(pv->handle, 0)) 
	{
		case WAIT_OBJECT_0:
			if (!ReleaseSemaphore(pv->handle, 1, &previous)) 
			{
				return lc_set_errno(EINVAL);
			}

			*value = previous + 1;
			return 0;

		case WAIT_TIMEOUT:
			*value = 0;
			return 0;

		default:
			return lc_set_errno(EINVAL);
	}
}


int sem_destroy(sem_t *sem)
{
	arch_sem_t *pv = (arch_sem_t *) sem;

	if (pv == NULL) 
	{
		return lc_set_errno(EINVAL);
	}

	if (CloseHandle(pv->handle) == 0) 
	{
		return lc_set_errno(EINVAL);
	}

	free(pv);
	*sem = NULL;
	return 0;
}

sem_t *sem_open(const char *name, int oflag, unsigned short mode, unsigned int value)
{
	int len;
	char buffer[512];
	arch_sem_t *pv;

	if (value > (unsigned int) SEM_VALUE_MAX || (len = strlen(name)) > (int) sizeof(buffer) - 8 || len < 1) 
	{
		lc_set_errno(EINVAL);
		return NULL;
	}

	if (NULL == (pv = (arch_sem_t *)calloc(1, sizeof(arch_sem_t)))) 
	{
		lc_set_errno(ENOMEM);
		return NULL;
	}

	memmove(buffer, "Global\\", 7);
	memmove(buffer + 7, name, len);
	buffer[len + 7] = '\0';

	if ((pv->handle = CreateSemaphoreA(NULL, value, SEM_VALUE_MAX, buffer)) == NULL) 
	{
		switch (GetLastError()) 
		{
			case ERROR_ACCESS_DENIED:
				lc_set_errno(EACCES);
				break;

			case ERROR_INVALID_HANDLE:
				lc_set_errno(ENOENT);
				break;

			default:
				lc_set_errno(ENOSPC);
				break;
		}

		free(pv);
		return NULL;
	}
	else 
	{
		if (GetLastError() == ERROR_ALREADY_EXISTS) 
		{
			if ((oflag & O_CREAT) && (oflag & O_EXCL)) 
			{
				CloseHandle(pv->handle);
				free(pv);
				lc_set_errno(EEXIST);
				return NULL;
			}

			return (sem_t *) pv;
		}
		else
		{
			if (!(oflag & O_CREAT)) 
			{
				free(pv);
				lc_set_errno(ENOENT);
				return NULL;
			}
		}
	}

	return (sem_t *) pv;
}

int sem_close(sem_t *sem) {	return sem_destroy(sem); }

int sem_unlink(const char *name) {
	return 0;
}
