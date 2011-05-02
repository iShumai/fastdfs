/**
* Copyright (C) 2008 Happy Fish / YuQing
*
* FastDFS may be copied only under the terms of the GNU General
* Public License V3, which may be found in the FastDFS source kit.
* Please visit the FastDFS Home Page http://www.csource.org/ for more detail.
**/

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>
#include "fdfs_define.h"
#include "logger.h"
#include "sockopt.h"
#include "fdfs_global.h"
#include "tracker_global.h"
#include "tracker_proto.h"
#include "tracker_mem.h"
#include "shared_func.h"
#include "pthread_func.h"

#define TRACKER_MEM_ALLOC_ONCE	2

#define GROUP_SECTION_NAME_GLOBAL            "Global"
#define GROUP_SECTION_NAME_PREFIX            "Group"
#define GROUP_SECTION_NO_FORMAT              "%03d"
#define GROUP_ITEM_GROUP_COUNT               "group_count"
#define GROUP_ITEM_GROUP_NAME                "group_name"
#define GROUP_ITEM_STORAGE_PORT              "storage_port"
#define GROUP_ITEM_STORAGE_HTTP_PORT         "storage_http_port"
#define GROUP_ITEM_STORE_PATH_COUNT          "store_path_count"
#define GROUP_ITEM_SUBDIR_COUNT_PER_PATH     "subdir_count_per_path"
#define GROUP_ITEM_CURRENT_TRUNK_FILE_ID     "current_trunk_file_id"

#define STORAGE_SECTION_NAME_GLOBAL            "Global"
#define STORAGE_SECTION_NAME_PREFIX            "Storage"
#define STORAGE_SECTION_NO_FORMAT              "%03d"
#define STORAGE_ITEM_STORAGE_COUNT             "storage_count"

#define STORAGE_ITEM_GROUP_NAME                "group_name"
#define STORAGE_ITEM_IP_ADDR                   "ip_addr"
#define STORAGE_ITEM_STATUS                    "status"
#define STORAGE_ITEM_DOMAIN_NAME               "domain_name"
#define STORAGE_ITEM_VERSION                   "version"
#define STORAGE_ITEM_SYNC_SRC_SERVER           "sync_src_server"
#define STORAGE_ITEM_SYNC_UNTIL_TIMESTAMP      "sync_until_timestamp"
#define STORAGE_ITEM_JOIN_TIME                 "join_time"
#define STORAGE_ITEM_TOTAL_MB                  "total_mb"
#define STORAGE_ITEM_FREE_MB                   "free_mb"
#define STORAGE_ITEM_CHANGELOG_OFFSET          "changelog_offset"
#define STORAGE_ITEM_STORE_PATH_COUNT          "store_path_count"
#define STORAGE_ITEM_SUBDIR_COUNT_PER_PATH     "subdir_count_per_path"
#define STORAGE_ITEM_UPLOAD_PRIORITY           "upload_priority"
#define STORAGE_ITEM_STORAGE_PORT              "storage_port"
#define STORAGE_ITEM_STORAGE_HTTP_PORT         "storage_http_port"
#define STORAGE_ITEM_TOTAL_UPLOAD_COUNT        "total_upload_count"
#define STORAGE_ITEM_SUCCESS_UPLOAD_COUNT      "success_upload_count"
#define STORAGE_ITEM_TOTAL_APPEND_COUNT        "total_append_count"
#define STORAGE_ITEM_SUCCESS_APPEND_COUNT      "success_append_count"
#define STORAGE_ITEM_TOTAL_SET_META_COUNT      "total_set_meta_count"
#define STORAGE_ITEM_SUCCESS_SET_META_COUNT    "success_set_meta_count"
#define STORAGE_ITEM_TOTAL_DELETE_COUNT        "total_delete_count"
#define STORAGE_ITEM_SUCCESS_DELETE_COUNT      "success_delete_count"
#define STORAGE_ITEM_TOTAL_DOWNLOAD_COUNT      "total_download_count"
#define STORAGE_ITEM_SUCCESS_DOWNLOAD_COUNT    "success_download_count"
#define STORAGE_ITEM_TOTAL_GET_META_COUNT      "total_get_meta_count"
#define STORAGE_ITEM_SUCCESS_GET_META_COUNT    "success_get_meta_count"
#define STORAGE_ITEM_TOTAL_CREATE_LINK_COUNT   "total_create_link_count"
#define STORAGE_ITEM_SUCCESS_CREATE_LINK_COUNT "success_create_link_count"
#define STORAGE_ITEM_TOTAL_DELETE_LINK_COUNT   "total_delete_link_count"
#define STORAGE_ITEM_SUCCESS_DELETE_LINK_COUNT "success_delete_link_count"
#define STORAGE_ITEM_TOTAL_UPLOAD_BYTES        "total_upload_bytes"
#define STORAGE_ITEM_SUCCESS_UPLOAD_BYTES      "success_upload_bytes"
#define STORAGE_ITEM_TOTAL_APPEND_BYTES        "total_append_bytes"
#define STORAGE_ITEM_SUCCESS_APPEND_BYTES      "success_append_bytes"
#define STORAGE_ITEM_TOTAL_DOWNLOAD_BYTES      "total_download_bytes"
#define STORAGE_ITEM_SUCCESS_DOWNLOAD_BYTES    "success_download_bytes"
#define STORAGE_ITEM_TOTAL_SYNC_IN_BYTES       "total_sync_in_bytes"
#define STORAGE_ITEM_SUCCESS_SYNC_IN_BYTES     "success_sync_in_bytes"
#define STORAGE_ITEM_TOTAL_SYNC_OUT_BYTES      "total_sync_out_bytes"
#define STORAGE_ITEM_SUCCESS_SYNC_OUT_BYTES    "success_sync_out_bytes"
#define STORAGE_ITEM_TOTAL_FILE_OPEN_COUNT     "total_file_open_count"
#define STORAGE_ITEM_SUCCESS_FILE_OPEN_COUNT   "success_file_open_count"
#define STORAGE_ITEM_TOTAL_FILE_READ_COUNT     "total_file_read_count"
#define STORAGE_ITEM_SUCCESS_FILE_READ_COUNT   "success_file_read_count"
#define STORAGE_ITEM_TOTAL_FILE_WRITE_COUNT    "total_file_write_count"
#define STORAGE_ITEM_SUCCESS_FILE_WRITE_COUNT  "success_file_write_count"
#define STORAGE_ITEM_LAST_SOURCE_UPDATE        "last_source_update"
#define STORAGE_ITEM_LAST_SYNC_UPDATE          "last_sync_update"
#define STORAGE_ITEM_LAST_SYNCED_TIMESTAMP     "last_synced_timestamp"
#define STORAGE_ITEM_LAST_HEART_BEAT_TIME      "last_heart_beat_time"

static pthread_mutex_t mem_thread_lock;
static pthread_mutex_t mem_file_lock;

int64_t g_changelog_fsize = 0; //storage server change log file size
static int changelog_fd = -1;  //storage server change log fd for write
static bool need_get_sys_files = true;
static bool get_sys_files_done = false;

static TrackerServerGroup tracker_group = {0, 0, -1, NULL};

static void tracker_mem_find_store_server(FDFSGroupInfo *pGroup);

static int _tracker_mem_add_storage(FDFSGroupInfo *pGroup, \
	FDFSStorageDetail **ppStorageServer, const char *ip_addr, \
	const bool bNeedSleep, bool *bInserted);

static int tracker_mem_add_storage(TrackerClientInfo *pClientInfo, \
		const char *ip_addr, const bool bNeedSleep, bool *bInserted);

static int tracker_mem_add_group_ex(FDFSGroups *pGroups, \
	TrackerClientInfo *pClientInfo, const char *group_name, \
	const bool bNeedSleep, bool *bInserted);

static int tracker_save_groups();

static int tracker_mem_destroy_groups(FDFSGroups *pGroups, const bool saveFiles);

char *g_tracker_sys_filenames[TRACKER_SYS_FILE_COUNT] = {
	STORAGE_GROUPS_LIST_FILENAME_NEW,
	STORAGE_SERVERS_LIST_FILENAME_NEW,
	STORAGE_SYNC_TIMESTAMP_FILENAME,
	STORAGE_SERVERS_CHANGELOG_FILENAME
};      
   
#define TRACKER_CHOWN(path, current_uid, current_gid) \
	if (!(g_run_by_gid == current_gid && g_run_by_uid == current_uid)) \
	{ \
		if (chown(path, g_run_by_uid, g_run_by_gid) != 0) \
		{ \
			logError("file: "__FILE__", line: %d, " \
				"chown \"%s\" fail, " \
				"errno: %d, error info: %s", \
				__LINE__, path, \
				errno, STRERROR(errno)); \
			return errno != 0 ? errno : EPERM; \
		} \
	}

#define TRACKER_FCHOWN(fd, path, current_uid, current_gid) \
	if (!(g_run_by_gid == current_gid && g_run_by_uid == current_uid)) \
	{ \
		if (fchown(fd, g_run_by_uid, g_run_by_gid) != 0) \
		{ \
			logError("file: "__FILE__", line: %d, " \
				"chown \"%s\" fail, " \
				"errno: %d, error info: %s", \
				__LINE__, path, \
				errno, STRERROR(errno)); \
			return errno != 0 ? errno : EPERM; \
		} \
	}


int tracker_mem_pthread_lock()
{
	int result;
	if ((result=pthread_mutex_lock(&mem_thread_lock)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call pthread_mutex_lock fail, " \
			"errno: %d, error info: %s", \
			__LINE__, result, STRERROR(result));
		return result;
	}

	return 0;
}

int tracker_mem_pthread_unlock()
{
	int result;
	if ((result=pthread_mutex_unlock(&mem_thread_lock)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call pthread_mutex_unlock fail, " \
			"errno: %d, error info: %s", \
			__LINE__, result, STRERROR(result));
		return result;
	}

	return 0;
}

int tracker_mem_file_lock()
{
	int result;
	if ((result=pthread_mutex_lock(&mem_file_lock)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call pthread_mutex_lock fail, " \
			"errno: %d, error info: %s", \
			__LINE__, result, STRERROR(result));
		return result;
	}

	return 0;
}

int tracker_mem_file_unlock()
{
	int result;
	if ((result=pthread_mutex_unlock(&mem_file_lock)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call pthread_mutex_unlock fail, " \
			"errno: %d, error info: %s", \
			__LINE__, result, STRERROR(result));
		return result;
	}

	return 0;
}

static int tracker_write_to_changelog(FDFSGroupInfo *pGroup, \
		FDFSStorageDetail *pStorage, const char *pArg)
{
	char buff[256];
	int len;
	int result;

	tracker_mem_file_lock();

	len = snprintf(buff, sizeof(buff), "%d %s %s %d %s\n", \
		(int)time(NULL), pGroup->group_name, pStorage->ip_addr, \
		pStorage->status, pArg != NULL ? pArg : "");

	if (write(changelog_fd, buff, len) != len)
	{
		tracker_mem_file_unlock();

		result = errno != 0 ? errno : EIO;
		logError("file: "__FILE__", line: %d, " \
			"write to file: %s fail, " \
			"errno: %d, error info: %s", \
			__LINE__, STORAGE_SERVERS_CHANGELOG_FILENAME, \
			result, STRERROR(result));

		return result;
	}

	g_changelog_fsize += len;
	result = fsync(changelog_fd);
	if (result != 0)
	{
		result = errno != 0 ? errno : EIO;
		logError("file: "__FILE__", line: %d, " \
			"call fsync of file: %s fail, " \
			"errno: %d, error info: %s", \
			__LINE__, STORAGE_SERVERS_CHANGELOG_FILENAME, \
			result, STRERROR(result));
	}

	tracker_mem_file_unlock();

	return result;
}

static int tracker_malloc_storage_path_mbs(FDFSStorageDetail *pStorage, \
		const int store_path_count)
{
	int alloc_bytes;

	if (store_path_count <= 0)
	{
		return 0;
	}

	alloc_bytes = sizeof(int64_t) * store_path_count;

	pStorage->path_total_mbs = (int64_t *)malloc(alloc_bytes);
	if (pStorage->path_total_mbs == NULL)
	{
		logError("file: "__FILE__", line: %d, " \
			"malloc %d bytes fail, " \
			"errno: %d, error info: %s", \
			__LINE__, alloc_bytes, errno, STRERROR(errno));
		return errno != 0 ? errno : ENOMEM;
	}

	pStorage->path_free_mbs = (int64_t *)malloc(alloc_bytes);
	if (pStorage->path_free_mbs == NULL)
	{
		free(pStorage->path_total_mbs);
		pStorage->path_total_mbs = NULL;

		logError("file: "__FILE__", line: %d, " \
			"malloc %d bytes fail, " \
			"errno: %d, error info: %s", \
			__LINE__, alloc_bytes, errno, STRERROR(errno));
		return errno != 0 ? errno : ENOMEM;
	}

	memset(pStorage->path_total_mbs, 0, alloc_bytes);
	memset(pStorage->path_free_mbs, 0, alloc_bytes);

	return 0;
}

static int tracker_realloc_storage_path_mbs(FDFSStorageDetail *pStorage, \
		const int old_store_path_count, const int new_store_path_count)
{
	int alloc_bytes;
	int copy_bytes;
	int64_t *new_path_total_mbs;
	int64_t *new_path_free_mbs;

	if (new_store_path_count <= 0)
	{
		return EINVAL;
	}

	alloc_bytes = sizeof(int64_t) * new_store_path_count;

	new_path_total_mbs = (int64_t *)malloc(alloc_bytes);
	if (new_path_total_mbs == NULL)
	{
		logError("file: "__FILE__", line: %d, " \
			"malloc %d bytes fail, " \
			"errno: %d, error info: %s", \
			__LINE__, alloc_bytes, errno, STRERROR(errno));
		return errno != 0 ? errno : ENOMEM;
	}

	new_path_free_mbs = (int64_t *)malloc(alloc_bytes);
	if (new_path_free_mbs == NULL)
	{
		free(new_path_total_mbs);

		logError("file: "__FILE__", line: %d, " \
			"malloc %d bytes fail, " \
			"errno: %d, error info: %s", \
			__LINE__, alloc_bytes, errno, STRERROR(errno));
		return errno != 0 ? errno : ENOMEM;
	}

	memset(new_path_total_mbs, 0, alloc_bytes);
	memset(new_path_free_mbs, 0, alloc_bytes);

	if (old_store_path_count == 0)
	{
		pStorage->path_total_mbs = new_path_total_mbs;
		pStorage->path_free_mbs = new_path_free_mbs;

		return 0;
	}

	copy_bytes = (old_store_path_count < new_store_path_count ? \
		old_store_path_count : new_store_path_count) * sizeof(int64_t);
	memcpy(new_path_total_mbs, pStorage->path_total_mbs, copy_bytes);
	memcpy(new_path_free_mbs, pStorage->path_free_mbs, copy_bytes);

	free(pStorage->path_total_mbs);
	free(pStorage->path_free_mbs);

	pStorage->path_total_mbs = new_path_total_mbs;
	pStorage->path_free_mbs = new_path_free_mbs;

	return 0;
}

static int tracker_realloc_group_path_mbs(FDFSGroupInfo *pGroup, \
		const int new_store_path_count)
{
	FDFSStorageDetail **ppStorage;
	FDFSStorageDetail **ppEnd;
	int result;

	ppEnd = pGroup->all_servers + pGroup->alloc_size;
	for (ppStorage=pGroup->all_servers; ppStorage<ppEnd; ppStorage++)
	{
		if ((result=tracker_realloc_storage_path_mbs(*ppStorage, \
			pGroup->store_path_count, new_store_path_count)) != 0)
		{
			return result;
		}
	}

	pGroup->store_path_count = new_store_path_count;

	return 0;
}

static int tracker_malloc_group_path_mbs(FDFSGroupInfo *pGroup)
{
	FDFSStorageDetail **ppStorage;
	FDFSStorageDetail **ppEnd;
	int result;

	ppEnd = pGroup->all_servers + pGroup->alloc_size;
	for (ppStorage=pGroup->all_servers; ppStorage<ppEnd; ppStorage++)
	{
		if ((result=tracker_malloc_storage_path_mbs(*ppStorage, \
				pGroup->store_path_count)) != 0)
		{
			return result;
		}
	}

	return 0;
}

static int tracker_malloc_all_group_path_mbs(FDFSGroups *pGroups)
{
	FDFSGroupInfo **ppGroup;
	FDFSGroupInfo **ppEnd;
	int result;

	ppEnd = pGroups->groups + pGroups->alloc_size;
	for (ppGroup=pGroups->groups; ppGroup<ppEnd; ppGroup++)
	{
		if ((*ppGroup)->store_path_count == 0)
		{
			continue;
		}

		if ((result=tracker_malloc_group_path_mbs(*ppGroup)) != 0)
		{
			return result;
		}
	}

	return 0;
}

static int tracker_load_groups_old(FDFSGroups *pGroups, const char *data_path)
{
#define STORAGE_DATA_GROUP_FIELDS	4

	FILE *fp;
	char szLine[256];
	char group_name[FDFS_GROUP_NAME_MAX_LEN + 1];
	char *fields[STORAGE_DATA_GROUP_FIELDS];
	int result;
	int col_count;
	TrackerClientInfo clientInfo;
	bool bInserted;

	if ((fp=fopen(STORAGE_GROUPS_LIST_FILENAME_OLD, "r")) == NULL)
	{
		logError("file: "__FILE__", line: %d, " \
			"open file \"%s/%s\" fail, " \
			"errno: %d, error info: %s", \
			__LINE__, data_path, STORAGE_GROUPS_LIST_FILENAME_OLD, \
			errno, STRERROR(errno));
		return errno != 0 ? errno : ENOENT;
	}

	result = 0;
	while (fgets(szLine, sizeof(szLine), fp) != NULL)
	{
		if (*szLine == '\0')
		{
			continue;
		}

		col_count = splitEx(szLine, STORAGE_DATA_FIELD_SEPERATOR, \
			fields, STORAGE_DATA_GROUP_FIELDS);
		if (col_count != STORAGE_DATA_GROUP_FIELDS && \
			col_count != STORAGE_DATA_GROUP_FIELDS - 2)
		{
			logError("file: "__FILE__", line: %d, " \
				"the format of the file \"%s/%s\" is invalid", \
				__LINE__, data_path, \
				STORAGE_GROUPS_LIST_FILENAME_OLD);
			result = errno != 0 ? errno : EINVAL;
			break;
		}
	
		memset(&clientInfo, 0, sizeof(TrackerClientInfo));
		snprintf(group_name, sizeof(group_name),\
				"%s", trim(fields[0]));
		if ((result=tracker_mem_add_group_ex(pGroups, &clientInfo, \
				group_name, false, &bInserted)) != 0)
		{
			break;
		}

		if (!bInserted)
		{
			logError("file: "__FILE__", line: %d, " \
				"in the file \"%s/%s\", " \
				"group \"%s\" is duplicate", \
				__LINE__, data_path, \
				STORAGE_GROUPS_LIST_FILENAME_OLD, \
				group_name);
			result = errno != 0 ? errno : EEXIST;
			break;
		}

		clientInfo.pGroup->storage_port = atoi(trim(fields[1]));
		if (col_count == STORAGE_DATA_GROUP_FIELDS - 2)
		{  //version < V1.12
			clientInfo.pGroup->store_path_count = 0;
			clientInfo.pGroup->subdir_count_per_path = 0;
		}
		else
		{
			clientInfo.pGroup->store_path_count = \
				atoi(trim(fields[2]));
			clientInfo.pGroup->subdir_count_per_path = \
				atoi(trim(fields[3]));
		}
	}

	fclose(fp);
	return result;
}

static int tracker_load_groups_new(FDFSGroups *pGroups, const char *data_path)
{
	IniContext iniContext;
	FDFSGroupInfo *pGroup;
	char *group_name;
	int group_count;
	int result;
	int i;
	char section_name[64];
	TrackerClientInfo clientInfo;
	bool bInserted;

	if (!fileExists(STORAGE_GROUPS_LIST_FILENAME_NEW) && \
	     fileExists(STORAGE_GROUPS_LIST_FILENAME_OLD))
	{
		logDebug("file: "__FILE__", line: %d, " \
			"convert old data file %s to new data file %s", \
			__LINE__, STORAGE_GROUPS_LIST_FILENAME_OLD, \
			STORAGE_GROUPS_LIST_FILENAME_NEW);
		if ((result=tracker_load_groups_old(pGroups, data_path)) == 0)
		{
			if ((result=tracker_save_groups()) == 0)
			{
				unlink(STORAGE_GROUPS_LIST_FILENAME_OLD);
			}
		}

		return result;
	}

	if ((result=iniLoadFromFile(STORAGE_GROUPS_LIST_FILENAME_NEW, \
			&iniContext)) != 0)
	{
		return result;
	}

	group_count = iniGetIntValue(GROUP_SECTION_NAME_GLOBAL, \
				GROUP_ITEM_GROUP_COUNT, \
                		&iniContext, -1);
	if (group_count < 0)
	{
		iniFreeContext(&iniContext);
		logError("file: "__FILE__", line: %d, " \
			"in the file \"%s/%s\", " \
			"item \"%s\" is not found", \
			__LINE__, data_path, \
			STORAGE_GROUPS_LIST_FILENAME_NEW, \
			GROUP_ITEM_GROUP_COUNT);
		return ENOENT;
	}

	for (i=1; i<=group_count; i++)
	{
		sprintf(section_name, "%s"GROUP_SECTION_NO_FORMAT, \
			GROUP_SECTION_NAME_PREFIX, i);

		group_name = iniGetStrValue(section_name, \
				GROUP_ITEM_GROUP_NAME, &iniContext);
		if (group_name == NULL)
		{
			logError("file: "__FILE__", line: %d, " \
				"in the file \"%s/%s\", " \
				"item \"%s\" is not found", \
				__LINE__, data_path, \
				STORAGE_GROUPS_LIST_FILENAME_NEW, \
				GROUP_ITEM_GROUP_NAME);
			result = ENOENT;
			break;
		}

		memset(&clientInfo, 0, sizeof(TrackerClientInfo));
		if ((result=tracker_mem_add_group_ex(pGroups, &clientInfo, \
				group_name, false, &bInserted)) != 0)
		{
			break;
		}

		if (!bInserted)
		{
			logError("file: "__FILE__", line: %d, " \
				"in the file \"%s/%s\", " \
				"group \"%s\" is duplicate", \
				__LINE__, data_path, \
				STORAGE_GROUPS_LIST_FILENAME_NEW, \
				group_name);
			result = errno != 0 ? errno : EEXIST;
			break;
		}

		pGroup = clientInfo.pGroup;
		pGroup->storage_port = iniGetIntValue(section_name, \
			GROUP_ITEM_STORAGE_PORT, &iniContext, 0);
		pGroup->storage_http_port = iniGetIntValue(section_name, \
			GROUP_ITEM_STORAGE_HTTP_PORT, &iniContext, 0);
		pGroup->store_path_count = iniGetIntValue(section_name, \
			GROUP_ITEM_STORE_PATH_COUNT, &iniContext, 0);
		pGroup->subdir_count_per_path = iniGetIntValue(section_name, \
			GROUP_ITEM_SUBDIR_COUNT_PER_PATH, &iniContext, 0);
		pGroup->current_trunk_file_id = iniGetIntValue(section_name, \
			GROUP_ITEM_CURRENT_TRUNK_FILE_ID, &iniContext, 0);
	}

	iniFreeContext(&iniContext);

	return result;
}

static int tracker_locate_storage_sync_server(FDFSGroups *pGroups, \
		FDFSStorageSync *pStorageSyncs, \
		const int nStorageSyncCount, const bool bLoadFromFile)
{
	FDFSGroupInfo *pGroup;
	FDFSStorageDetail *pStorage;
	FDFSStorageSync *pSyncServer;
	FDFSStorageSync *pSyncEnd;

	pSyncEnd = pStorageSyncs + nStorageSyncCount;
	for (pSyncServer=pStorageSyncs; pSyncServer<pSyncEnd; pSyncServer++)
	{
		pGroup = tracker_mem_get_group_ex(pGroups, \
				pSyncServer->group_name);
		if (pGroup == NULL)
		{
			continue;
		}

		pStorage=tracker_mem_get_storage(pGroup, pSyncServer->ip_addr);
		if (pStorage == NULL)
		{
			continue;
		}

		pStorage->psync_src_server = tracker_mem_get_storage(pGroup, \
			pSyncServer->sync_src_ip_addr);
		if (pStorage->psync_src_server == NULL)
		{
			char buff[MAX_PATH_SIZE+64];
			if (bLoadFromFile)
			{
				snprintf(buff, sizeof(buff), \
					"in the file \"%s/data/%s\", ", \
					g_fdfs_base_path, \
					STORAGE_SERVERS_LIST_FILENAME_NEW);
			}
			else
			{
				buff[0] = '\0';
			}

			logError("file: "__FILE__", line: %d, " \
				"%sgroup_name: %s, storage server \"%s:%d\" " \
				"does not exist", \
				__LINE__, buff, pSyncServer->group_name, \
				pSyncServer->sync_src_ip_addr, \
				pGroup->storage_port);

			return ENOENT;
		}
	}

	return 0;
}

static int tracker_load_storages_old(FDFSGroups *pGroups, const char *data_path)
{
#define STORAGE_DATA_SERVER_FIELDS	22

	FILE *fp;
	char szLine[256];
	char group_name[FDFS_GROUP_NAME_MAX_LEN + 1];
	char *fields[STORAGE_DATA_SERVER_FIELDS];
	char ip_addr[IP_ADDRESS_SIZE];
	char *psync_src_ip_addr;
	FDFSStorageDetail *pStorage;
	FDFSStorageSync *pStorageSyncs;
	int nStorageSyncSize;
	int nStorageSyncCount;
	int cols;
	int result;
	TrackerClientInfo clientInfo;
	bool bInserted;

	if ((fp=fopen(STORAGE_SERVERS_LIST_FILENAME_OLD, "r")) == NULL)
	{
		logError("file: "__FILE__", line: %d, " \
			"open file \"%s/%s\" fail, " \
			"errno: %d, error info: %s", \
			__LINE__, data_path, STORAGE_SERVERS_LIST_FILENAME_OLD, \
			errno, STRERROR(errno));
		return errno != 0 ? errno : ENOENT;
	}

	nStorageSyncSize = 0;
	nStorageSyncCount = 0;
	pStorageSyncs = NULL;
	result = 0;
	while (fgets(szLine, sizeof(szLine), fp) != NULL)
	{
		if (*szLine == '\0')
		{
			continue;
		}

		cols = splitEx(szLine, STORAGE_DATA_FIELD_SEPERATOR, \
				fields, STORAGE_DATA_SERVER_FIELDS);
		if (cols != STORAGE_DATA_SERVER_FIELDS && \
		    cols != STORAGE_DATA_SERVER_FIELDS - 2 && \
		    cols != STORAGE_DATA_SERVER_FIELDS - 4 && \
		    cols != STORAGE_DATA_SERVER_FIELDS - 5)
		{
			logError("file: "__FILE__", line: %d, " \
				"the format of the file \"%s/%s\" is invalid" \
				", colums: %d != expect colums: " \
				"%d or %d or %d or %d", \
				__LINE__, data_path, \
				STORAGE_SERVERS_LIST_FILENAME_OLD, \
				cols, STORAGE_DATA_SERVER_FIELDS, \
				STORAGE_DATA_SERVER_FIELDS - 2, \
				STORAGE_DATA_SERVER_FIELDS - 4, \
				STORAGE_DATA_SERVER_FIELDS - 5);
			result = EINVAL;
			break;
		}
	
		memset(&clientInfo, 0, sizeof(TrackerClientInfo));
		snprintf(group_name, sizeof(group_name), "%s", trim(fields[0]));
		snprintf(ip_addr, sizeof(ip_addr), "%s", trim(fields[1]));
		if ((clientInfo.pGroup=tracker_mem_get_group_ex(pGroups, \
						group_name)) == NULL)
		{
			logError("file: "__FILE__", line: %d, " \
				"in the file \"%s/%s\", " \
				"group \"%s\" is not found", \
				__LINE__, data_path, \
				STORAGE_SERVERS_LIST_FILENAME_OLD, \
				group_name);
			result = errno != 0 ? errno : ENOENT;
			break;
		}

		if ((result=tracker_mem_add_storage(&clientInfo, ip_addr, \
				false, &bInserted)) != 0)
		{
			break;
		}

		if (!bInserted)
		{
			logError("file: "__FILE__", line: %d, " \
				"in the file \"%s/%s\", " \
				"storage \"%s\" is duplicate", \
				__LINE__, data_path, \
				STORAGE_SERVERS_LIST_FILENAME_OLD, ip_addr);
			result = errno != 0 ? errno : EEXIST;
			break;
		}
	
		pStorage = clientInfo.pStorage;
		pStorage->status = atoi(trim_left(fields[2]));
		if (!((pStorage->status == \
				FDFS_STORAGE_STATUS_WAIT_SYNC) || \
			(pStorage->status == \
				FDFS_STORAGE_STATUS_SYNCING) || \
			(pStorage->status == \
				FDFS_STORAGE_STATUS_INIT)))
		{
			pStorage->status = \
				FDFS_STORAGE_STATUS_OFFLINE;
		}

		psync_src_ip_addr = trim(fields[3]);
		pStorage->sync_until_timestamp = atoi( \
					trim_left(fields[4]));
		pStorage->stat.total_upload_count = strtoll( \
					trim_left(fields[5]), NULL, 10);
		pStorage->stat.success_upload_count = strtoll( \
					trim_left(fields[6]), NULL, 10);
		pStorage->stat.total_set_meta_count = strtoll( \
					trim_left(fields[7]), NULL, 10);
		pStorage->stat.success_set_meta_count = strtoll( \
					trim_left(fields[8]), NULL, 10);
		pStorage->stat.total_delete_count = strtoll( \
					trim_left(fields[9]), NULL, 10);
		pStorage->stat.success_delete_count = strtoll( \
					trim_left(fields[10]), NULL, 10);
		pStorage->stat.total_download_count = strtoll( \
					trim_left(fields[11]), NULL, 10);
		pStorage->stat.success_download_count = strtoll( \
					trim_left(fields[12]), NULL, 10);
		pStorage->stat.total_get_meta_count = strtoll( \
					trim_left(fields[13]), NULL, 10);
		pStorage->stat.success_get_meta_count = strtoll( \
					trim_left(fields[14]), NULL, 10);
		pStorage->stat.last_source_update = atoi( \
					trim_left(fields[15]));
		pStorage->stat.last_sync_update = atoi( \
					trim_left(fields[16]));
		if (cols > STORAGE_DATA_SERVER_FIELDS - 5)
		{
			pStorage->changelog_offset = strtoll( \
					trim_left(fields[17]), NULL, 10);
			if (pStorage->changelog_offset < 0)
			{
				pStorage->changelog_offset = 0;
			}
			if (pStorage->changelog_offset > \
				g_changelog_fsize)
			{
				pStorage->changelog_offset = \
					g_changelog_fsize;
			}

			if (cols > STORAGE_DATA_SERVER_FIELDS - 4)
			{
				pStorage->storage_port = \
					atoi(trim_left(fields[18]));
				pStorage->storage_http_port = \
					atoi(trim_left(fields[19]));
				if (cols > STORAGE_DATA_SERVER_FIELDS - 2)
				{
					pStorage->join_time = \
					(time_t)atoi(trim_left(fields[20]));

					snprintf(pStorage->version, \
					sizeof(pStorage->version), 
					 "%s", trim(fields[21]));
				}
			}
		}

		if (*psync_src_ip_addr == '\0')
		{
			continue;
		}

		if (nStorageSyncSize <= nStorageSyncCount)
		{
			nStorageSyncSize += 8;
			pStorageSyncs = (FDFSStorageSync *)realloc( \
				pStorageSyncs, \
				sizeof(FDFSStorageSync) * nStorageSyncSize);
			if (pStorageSyncs == NULL)
			{
				result = errno != 0 ? errno : ENOMEM;
				logError("file: "__FILE__", line: %d, " \
					"realloc %d bytes fail", __LINE__, \
					(int)sizeof(FDFSStorageSync) * \
					nStorageSyncSize);
				break;
			}
		}

		strcpy(pStorageSyncs[nStorageSyncCount].group_name, \
			clientInfo.pGroup->group_name);
		strcpy(pStorageSyncs[nStorageSyncCount].ip_addr, \
			pStorage->ip_addr);
		snprintf(pStorageSyncs[nStorageSyncCount].sync_src_ip_addr, \
			IP_ADDRESS_SIZE, "%s", psync_src_ip_addr);

		nStorageSyncCount++;

	}

	fclose(fp);

	if (pStorageSyncs == NULL)
	{
		return result;
	}

	if (result != 0)
	{
		free(pStorageSyncs);
		return result;
	}

	result = tracker_locate_storage_sync_server(pGroups, pStorageSyncs, \
			nStorageSyncCount, true);
	free(pStorageSyncs);
	return result;
}

static int tracker_load_storages_new(FDFSGroups *pGroups, const char *data_path)
{
	IniContext iniContext;
	char *group_name;
	char *ip_addr;
	char *psync_src_ip_addr;
	char *pValue;
	FDFSStorageDetail *pStorage;
	FDFSStorageStat *pStat;
	FDFSStorageSync *pStorageSyncs;
	int nStorageSyncSize;
	int nStorageSyncCount;
	int storage_count;
	int i;
	int result;
	char section_name[64];
	TrackerClientInfo clientInfo;
	bool bInserted;

	if (!fileExists(STORAGE_SERVERS_LIST_FILENAME_NEW) && \
	     fileExists(STORAGE_SERVERS_LIST_FILENAME_OLD))
	{
		logDebug("file: "__FILE__", line: %d, " \
			"convert old data file %s to new data file %s", \
			__LINE__, STORAGE_SERVERS_LIST_FILENAME_OLD, \
			STORAGE_SERVERS_LIST_FILENAME_NEW);
		if ((result=tracker_load_storages_old(pGroups, data_path)) == 0)
		{
			if ((result=tracker_save_storages()) == 0)
			{
				unlink(STORAGE_SERVERS_LIST_FILENAME_OLD);
			}
		}

		return result;
	}

	if ((result=iniLoadFromFile(STORAGE_SERVERS_LIST_FILENAME_NEW, \
			&iniContext)) != 0)
	{
		return result;
	}

	storage_count = iniGetIntValue(STORAGE_SECTION_NAME_GLOBAL, \
				STORAGE_ITEM_STORAGE_COUNT, \
                		&iniContext, -1);
	if (storage_count < 0)
	{
		iniFreeContext(&iniContext);
		logError("file: "__FILE__", line: %d, " \
			"in the file \"%s/%s\", " \
			"item \"%s\" is not found", \
			__LINE__, data_path, \
			STORAGE_SERVERS_LIST_FILENAME_NEW, \
			STORAGE_ITEM_STORAGE_COUNT);
		return ENOENT;
	}

	nStorageSyncSize = 0;
	nStorageSyncCount = 0;
	pStorageSyncs = NULL;
	result = 0;
	for (i=1; i<=storage_count; i++)
	{
		sprintf(section_name, "%s"STORAGE_SECTION_NO_FORMAT, \
			STORAGE_SECTION_NAME_PREFIX, i);

		group_name = iniGetStrValue(section_name, \
				STORAGE_ITEM_GROUP_NAME, &iniContext);
		if (group_name == NULL)
		{
			logError("file: "__FILE__", line: %d, " \
				"in the file \"%s/%s\", " \
				"item \"%s\" is not found", \
				__LINE__, data_path, \
				STORAGE_SERVERS_LIST_FILENAME_NEW, \
				STORAGE_ITEM_GROUP_NAME);
			result = ENOENT;
			break;
		}

		ip_addr = iniGetStrValue(section_name, \
				STORAGE_ITEM_IP_ADDR, &iniContext);
		if (ip_addr == NULL)
		{
			logError("file: "__FILE__", line: %d, " \
				"in the file \"%s/%s\", " \
				"item \"%s\" is not found", \
				__LINE__, data_path, \
				STORAGE_SERVERS_LIST_FILENAME_NEW, \
				STORAGE_ITEM_IP_ADDR);
			result = ENOENT;
			break;
		}

		memset(&clientInfo, 0, sizeof(TrackerClientInfo));
		if ((clientInfo.pGroup=tracker_mem_get_group_ex(pGroups, \
						group_name)) == NULL)
		{
			logError("file: "__FILE__", line: %d, " \
				"in the file \"%s/%s\", " \
				"group \"%s\" is not found", \
				__LINE__, data_path, \
				STORAGE_SERVERS_LIST_FILENAME_NEW, \
				group_name);
			result = errno != 0 ? errno : ENOENT;
			break;
		}

		if ((result=tracker_mem_add_storage(&clientInfo, ip_addr, \
				false, &bInserted)) != 0)
		{
			break;
		}

		if (!bInserted)
		{
			logError("file: "__FILE__", line: %d, " \
				"in the file \"%s/%s\", " \
				"storage \"%s\" is duplicate", \
				__LINE__, data_path, \
				STORAGE_SERVERS_LIST_FILENAME_NEW, ip_addr);
			result = errno != 0 ? errno : EEXIST;
			break;
		}
		
		pStorage = clientInfo.pStorage;
		pStat = &(pStorage->stat);
		pStorage->status = iniGetIntValue(section_name, \
				STORAGE_ITEM_STATUS, &iniContext, 0);

		pValue = iniGetStrValue(section_name, \
				STORAGE_ITEM_DOMAIN_NAME, &iniContext);
		if (pValue != NULL)
		{
			snprintf(pStorage->domain_name, \
				sizeof(pStorage->domain_name), "%s", pValue);
		}

		pValue = iniGetStrValue(section_name, \
				STORAGE_ITEM_VERSION, &iniContext);
		if (pValue != NULL)
		{
			snprintf(pStorage->version, \
				sizeof(pStorage->version), "%s", pValue);
		}

		if (!((pStorage->status == \
				FDFS_STORAGE_STATUS_WAIT_SYNC) || \
			(pStorage->status == \
				FDFS_STORAGE_STATUS_SYNCING) || \
			(pStorage->status == \
				FDFS_STORAGE_STATUS_INIT)))
		{
			pStorage->status = FDFS_STORAGE_STATUS_OFFLINE;
		}

		psync_src_ip_addr = iniGetStrValue(section_name, \
				STORAGE_ITEM_SYNC_SRC_SERVER, &iniContext);
		if (psync_src_ip_addr == NULL)
		{
			psync_src_ip_addr = "";
		}

		pStorage->sync_until_timestamp = iniGetIntValue(section_name, \
			STORAGE_ITEM_SYNC_UNTIL_TIMESTAMP, &iniContext, 0);
		pStorage->join_time = iniGetIntValue(section_name, \
			STORAGE_ITEM_JOIN_TIME, &iniContext, 0);
		pStorage->total_mb = iniGetInt64Value(section_name, \
			STORAGE_ITEM_TOTAL_MB, &iniContext, 0);
		pStorage->free_mb = iniGetInt64Value(section_name, \
			STORAGE_ITEM_FREE_MB, &iniContext, 0);
		pStorage->store_path_count = iniGetIntValue(section_name, \
			STORAGE_ITEM_STORE_PATH_COUNT, &iniContext, 0);
		pStorage->subdir_count_per_path = iniGetIntValue(section_name, \
			STORAGE_ITEM_SUBDIR_COUNT_PER_PATH, &iniContext, 0);
		pStorage->upload_priority = iniGetIntValue(section_name, \
			STORAGE_ITEM_UPLOAD_PRIORITY, &iniContext, 0);
		pStorage->storage_port = iniGetIntValue(section_name, \
			STORAGE_ITEM_STORAGE_PORT, &iniContext, 0);
		pStorage->storage_http_port = iniGetIntValue(section_name, \
			STORAGE_ITEM_STORAGE_HTTP_PORT, &iniContext, 0);
		pStat->total_upload_count = iniGetInt64Value(section_name, \
			STORAGE_ITEM_TOTAL_UPLOAD_COUNT, &iniContext, 0);
		pStat->success_upload_count = iniGetInt64Value(section_name, \
			STORAGE_ITEM_SUCCESS_UPLOAD_COUNT, &iniContext, 0);
		pStat->total_append_count = iniGetInt64Value(section_name, \
			STORAGE_ITEM_TOTAL_APPEND_COUNT, &iniContext, 0);
		pStat->success_append_count = iniGetInt64Value(section_name, \
			STORAGE_ITEM_SUCCESS_APPEND_COUNT, &iniContext, 0);
		pStat->total_set_meta_count = iniGetInt64Value(section_name, \
			STORAGE_ITEM_TOTAL_SET_META_COUNT, &iniContext, 0);
		pStat->success_set_meta_count = iniGetInt64Value(section_name, \
			STORAGE_ITEM_SUCCESS_SET_META_COUNT, &iniContext, 0);
		pStat->total_delete_count = iniGetInt64Value(section_name, \
			STORAGE_ITEM_TOTAL_DELETE_COUNT, &iniContext, 0);
		pStat->success_delete_count = iniGetInt64Value(section_name, \
			STORAGE_ITEM_SUCCESS_DELETE_COUNT, &iniContext, 0);
		pStat->total_download_count = iniGetInt64Value(section_name, \
			STORAGE_ITEM_TOTAL_DOWNLOAD_COUNT, &iniContext, 0);
		pStat->success_download_count = iniGetInt64Value(section_name, \
			STORAGE_ITEM_SUCCESS_DOWNLOAD_COUNT, &iniContext, 0);
		pStat->total_get_meta_count = iniGetInt64Value(section_name, \
			STORAGE_ITEM_TOTAL_GET_META_COUNT, &iniContext, 0);
		pStat->success_get_meta_count = iniGetInt64Value(section_name, \
			STORAGE_ITEM_SUCCESS_GET_META_COUNT, &iniContext, 0);
		pStat->total_create_link_count = iniGetInt64Value(section_name, \
			STORAGE_ITEM_TOTAL_CREATE_LINK_COUNT, &iniContext, 0);
		pStat->success_create_link_count = iniGetInt64Value(section_name, \
			STORAGE_ITEM_SUCCESS_CREATE_LINK_COUNT, &iniContext, 0);
		pStat->total_delete_link_count = iniGetInt64Value(section_name, \
			STORAGE_ITEM_TOTAL_DELETE_LINK_COUNT, &iniContext, 0);
		pStat->success_delete_link_count = iniGetInt64Value(section_name, \
			STORAGE_ITEM_SUCCESS_DELETE_LINK_COUNT, &iniContext, 0);
		pStat->total_upload_bytes = iniGetInt64Value(section_name, \
			STORAGE_ITEM_TOTAL_UPLOAD_BYTES, &iniContext, 0);
		pStat->success_upload_bytes = iniGetInt64Value(section_name, \
			STORAGE_ITEM_SUCCESS_UPLOAD_BYTES, &iniContext, 0);
		pStat->total_append_bytes = iniGetInt64Value(section_name, \
			STORAGE_ITEM_TOTAL_APPEND_BYTES, &iniContext, 0);
		pStat->success_append_bytes = iniGetInt64Value(section_name, \
			STORAGE_ITEM_SUCCESS_APPEND_BYTES, &iniContext, 0);
		pStat->total_download_bytes = iniGetInt64Value(section_name, \
			STORAGE_ITEM_TOTAL_DOWNLOAD_BYTES, &iniContext, 0);
		pStat->success_download_bytes = iniGetInt64Value(section_name, \
			STORAGE_ITEM_SUCCESS_DOWNLOAD_BYTES, &iniContext, 0);
		pStat->total_sync_in_bytes = iniGetInt64Value(section_name, \
			STORAGE_ITEM_TOTAL_SYNC_IN_BYTES, &iniContext, 0);
		pStat->success_sync_in_bytes = iniGetInt64Value(section_name, \
			STORAGE_ITEM_SUCCESS_SYNC_IN_BYTES, &iniContext, 0);
		pStat->total_sync_out_bytes = iniGetInt64Value(section_name, \
			STORAGE_ITEM_TOTAL_SYNC_OUT_BYTES, &iniContext, 0);
		pStat->success_sync_out_bytes = iniGetInt64Value(section_name, \
			STORAGE_ITEM_SUCCESS_SYNC_OUT_BYTES, &iniContext, 0);
		pStat->total_file_open_count = iniGetInt64Value(section_name, \
			STORAGE_ITEM_TOTAL_FILE_OPEN_COUNT, &iniContext, 0);
		pStat->success_file_open_count = iniGetInt64Value(section_name, \
			STORAGE_ITEM_SUCCESS_FILE_OPEN_COUNT, &iniContext, 0);
		pStat->total_file_read_count = iniGetInt64Value(section_name, \
			STORAGE_ITEM_TOTAL_FILE_READ_COUNT, &iniContext, 0);
		pStat->success_file_read_count = iniGetInt64Value(section_name, \
			STORAGE_ITEM_SUCCESS_FILE_READ_COUNT, &iniContext, 0);
		pStat->total_file_write_count = iniGetInt64Value(section_name, \
			STORAGE_ITEM_TOTAL_FILE_WRITE_COUNT, &iniContext, 0);
		pStat->success_file_write_count = iniGetInt64Value(section_name, \
			STORAGE_ITEM_SUCCESS_FILE_WRITE_COUNT, &iniContext, 0);
		pStat->last_source_update = iniGetIntValue(section_name, \
			STORAGE_ITEM_LAST_SOURCE_UPDATE, &iniContext, 0);
		pStat->last_sync_update = iniGetIntValue(section_name, \
			STORAGE_ITEM_LAST_SYNC_UPDATE, &iniContext, 0);
		pStat->last_synced_timestamp = iniGetIntValue(section_name, \
			STORAGE_ITEM_LAST_SYNCED_TIMESTAMP, &iniContext, 0);
		pStat->last_heart_beat_time = iniGetIntValue(section_name, \
			STORAGE_ITEM_LAST_HEART_BEAT_TIME, &iniContext, 0);
		pStorage->changelog_offset = iniGetInt64Value(section_name, \
			STORAGE_ITEM_CHANGELOG_OFFSET, &iniContext, 0);

		if (*psync_src_ip_addr == '\0')
		{
			continue;
		}

		if (nStorageSyncSize <= nStorageSyncCount)
		{
			if (nStorageSyncSize == 0)
			{
				nStorageSyncSize = 8;
			}
			else
			{
				nStorageSyncSize *= 2;
			}
			pStorageSyncs = (FDFSStorageSync *)realloc( \
				pStorageSyncs, \
				sizeof(FDFSStorageSync) * nStorageSyncSize);
			if (pStorageSyncs == NULL)
			{
				result = errno != 0 ? errno : ENOMEM;
				logError("file: "__FILE__", line: %d, " \
					"realloc %d bytes fail", __LINE__, \
					(int)sizeof(FDFSStorageSync) * \
					nStorageSyncSize);
				break;
			}
		}

		strcpy(pStorageSyncs[nStorageSyncCount].group_name, \
			clientInfo.pGroup->group_name);
		strcpy(pStorageSyncs[nStorageSyncCount].ip_addr, \
			pStorage->ip_addr);
		snprintf(pStorageSyncs[nStorageSyncCount].sync_src_ip_addr, \
			IP_ADDRESS_SIZE, "%s", psync_src_ip_addr);

		nStorageSyncCount++;

	}

	iniFreeContext(&iniContext);

	if (pStorageSyncs == NULL)
	{
		return result;
	}

	if (result != 0)
	{
		free(pStorageSyncs);
		return result;
	}

	result = tracker_locate_storage_sync_server(pGroups, pStorageSyncs, \
			nStorageSyncCount, true);
	free(pStorageSyncs);
	return result;
}

static int tracker_load_sync_timestamps(FDFSGroups *pGroups, const char *data_path)
{
#define STORAGE_SYNC_TIME_MAX_FIELDS	2 + FDFS_MAX_SERVERS_EACH_GROUP

	FILE *fp;
	char szLine[512];
	char group_name[FDFS_GROUP_NAME_MAX_LEN + 1];
	char previous_group_name[FDFS_GROUP_NAME_MAX_LEN + 1];
	char src_ip_addr[IP_ADDRESS_SIZE];
	char *fields[STORAGE_SYNC_TIME_MAX_FIELDS];
	FDFSGroupInfo **ppGroup;
	FDFSGroupInfo **ppEnd;
	FDFSGroupInfo *pGroup;
	int cols;
	int src_index;
	int dest_index;
	int curr_synced_timestamp;
	int result;

	if (!fileExists(STORAGE_SYNC_TIMESTAMP_FILENAME))
	{
		return 0;
	}

	if ((fp=fopen(STORAGE_SYNC_TIMESTAMP_FILENAME, "r")) == NULL)
	{
		logError("file: "__FILE__", line: %d, " \
			"open file \"%s/%s\" fail, " \
			"errno: %d, error info: %s", \
			__LINE__, data_path, STORAGE_SYNC_TIMESTAMP_FILENAME, \
			errno, STRERROR(errno));
		return errno != 0 ? errno : ENOENT;
	}

	pGroup = NULL;
	src_index = 0;
	*previous_group_name = '\0';
	result = 0;
	while (fgets(szLine, sizeof(szLine), fp) != NULL)
	{
		if (*szLine == '\0' || *szLine == '\n')
		{
			continue;
		}

		if ((cols=splitEx(szLine, STORAGE_DATA_FIELD_SEPERATOR, \
			fields, STORAGE_SYNC_TIME_MAX_FIELDS)) <= 2)
		{
			logError("file: "__FILE__", line: %d, " \
				"the format of the file \"%s/%s\" is invalid" \
				", colums: %d <= 2", \
				__LINE__, data_path, \
				STORAGE_SYNC_TIMESTAMP_FILENAME, cols);
			result = errno != 0 ? errno : EINVAL;
			break;
		}
	
		snprintf(group_name, sizeof(group_name), \
				"%s", trim(fields[0]));
		snprintf(src_ip_addr, sizeof(src_ip_addr), \
				"%s", trim(fields[1]));
		if (strcmp(group_name, previous_group_name) != 0 || \
			pGroup == NULL)
		{
			if ((pGroup=tracker_mem_get_group_ex(pGroups, \
						group_name)) == NULL)
			{
				logError("file: "__FILE__", line: %d, " \
					"in the file \"%s/%s\", " \
					"group \"%s\" is not found", \
					__LINE__, data_path, \
					STORAGE_SYNC_TIMESTAMP_FILENAME, \
					group_name);
				result = errno != 0 ? errno : ENOENT;
				break;
			}

			strcpy(previous_group_name, group_name);
			src_index = 0;
		}
		
		if (src_index >= pGroup->count)
		{
			logError("file: "__FILE__", line: %d, " \
				"the format of the file \"%s/%s\" is invalid" \
				", group: %s, row count:%d > server count:%d",\
				__LINE__, data_path, \
				STORAGE_SYNC_TIMESTAMP_FILENAME, \
				group_name, src_index+1, pGroup->count);
			result = errno != 0 ? errno : EINVAL;
			break;
		}

		if (strcmp(pGroup->all_servers[src_index]->ip_addr, \
			src_ip_addr) != 0)
		{
			logError("file: "__FILE__", line: %d, " \
				"in data file: \"%s/%s\", " \
				"group: %s, src server ip: %s != %s",\
				__LINE__, data_path, \
				STORAGE_SYNC_TIMESTAMP_FILENAME, \
				group_name, src_ip_addr, \
				pGroup->all_servers[src_index]->ip_addr);
			result = errno != 0 ? errno : EINVAL;
			break;
		}

		if (cols > pGroup->count + 2)
		{
			logError("file: "__FILE__", line: %d, " \
				"the format of the file \"%s/%s\" is invalid" \
				", group_name: %s, colums: %d > %d", \
				__LINE__, data_path, \
				STORAGE_SYNC_TIMESTAMP_FILENAME, \
				group_name, cols, pGroup->count + 2);
			result = errno != 0 ? errno : EINVAL;
			break;
		}

		for (dest_index=0; dest_index<cols-2; dest_index++)
		{
			pGroup->last_sync_timestamps[src_index][dest_index] = \
				atoi(trim_left(fields[2 + dest_index]));
		}

		src_index++;
	}

	fclose(fp);

	if (result != 0)
	{
		return result;
	}

	ppEnd = pGroups->groups + pGroups->count;
	for (ppGroup=pGroups->groups; ppGroup<ppEnd; ppGroup++)
	{
		if ((*ppGroup)->count <= 1)
		{
			continue;
		}

		for (dest_index=0; dest_index<(*ppGroup)->count; dest_index++)
		{
			if (pGroups->store_server == FDFS_STORE_SERVER_ROUND_ROBIN)
			{
				int min_synced_timestamp;

				min_synced_timestamp = 0;
				for (src_index=0; src_index<(*ppGroup)->count; \
					src_index++)
				{
					if (src_index == dest_index)
					{
						continue;
					}

					curr_synced_timestamp = \
						(*ppGroup)->last_sync_timestamps \
							[src_index][dest_index];
					if (curr_synced_timestamp == 0)
					{
						continue;
					}

					if (min_synced_timestamp == 0)
					{
						min_synced_timestamp = \
							curr_synced_timestamp;
					}
					else if (curr_synced_timestamp < \
						min_synced_timestamp)
					{
						min_synced_timestamp = \
							curr_synced_timestamp;
					}
				}

				(*ppGroup)->all_servers[dest_index]->stat. \
					last_synced_timestamp = min_synced_timestamp;
			}
			else
			{
				int max_synced_timestamp;

				max_synced_timestamp = 0;
				for (src_index=0; src_index<(*ppGroup)->count; \
					src_index++)
				{
					if (src_index == dest_index)
					{
						continue;
					}

					curr_synced_timestamp = \
						(*ppGroup)->last_sync_timestamps \
							[src_index][dest_index];
					if (curr_synced_timestamp > \
						max_synced_timestamp)
					{
						max_synced_timestamp = \
							curr_synced_timestamp;
					}
				}

				(*ppGroup)->all_servers[dest_index]->stat. \
					last_synced_timestamp = max_synced_timestamp;
			}
		}
	}

	return result;
}

static int tracker_load_data(FDFSGroups *pGroups)
{
	char data_path[MAX_PATH_SIZE];
	int result;

	snprintf(data_path, sizeof(data_path), "%s/data", g_fdfs_base_path);
	if (!fileExists(data_path))
	{
		if (mkdir(data_path, 0755) != 0)
		{
			logError("file: "__FILE__", line: %d, " \
				"mkdir \"%s\" fail, " \
				"errno: %d, error info: %s", \
				__LINE__, data_path, errno, STRERROR(errno));
			return errno != 0 ? errno : ENOENT;
		}
		TRACKER_CHOWN(data_path, geteuid(), getegid())
	}

	if (chdir(data_path) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"chdir \"%s\" fail, " \
			"errno: %d, error info: %s", \
			__LINE__, data_path, errno, STRERROR(errno));
		return errno != 0 ? errno : ENOENT;
	}

	if (!fileExists(STORAGE_GROUPS_LIST_FILENAME_OLD) && \
	    !fileExists(STORAGE_GROUPS_LIST_FILENAME_NEW))
	{
		return 0;
	}

	if ((result=tracker_load_groups_new(pGroups, data_path)) != 0)
	{
		return result;
	}

	if ((result=tracker_load_storages_new(pGroups, data_path)) != 0)
	{
		return result;
	}

	if ((result=tracker_malloc_all_group_path_mbs(pGroups)) != 0)
	{
		return result;
	}

	if ((result=tracker_load_sync_timestamps(pGroups, data_path)) != 0)
	{
		return result;
	}

	return 0;
}

static int tracker_save_groups()
{
	char tmpFilename[MAX_PATH_SIZE];
	char trueFilename[MAX_PATH_SIZE];
	char buff[FDFS_GROUP_NAME_MAX_LEN + 256];
	int fd;
	FDFSGroupInfo **ppGroup;
	FDFSGroupInfo **ppEnd;
	int result;
	int len;

	tracker_mem_file_lock();

	snprintf(trueFilename, sizeof(trueFilename), "%s/data/%s", \
		g_fdfs_base_path, STORAGE_GROUPS_LIST_FILENAME_NEW);
	snprintf(tmpFilename, sizeof(tmpFilename), "%s.tmp", trueFilename);
	if ((fd=open(tmpFilename, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
	{
		tracker_mem_file_unlock();

		logError("file: "__FILE__", line: %d, " \
			"open \"%s\" fail, " \
			"errno: %d, error info: %s", \
			__LINE__, tmpFilename, errno, STRERROR(errno));
		return errno != 0 ? errno : ENOENT;
	}

	len = sprintf(buff, \
			"# global section\n" \
			"[%s]\n" \
			"\t%s=%d\n\n", \
			GROUP_SECTION_NAME_GLOBAL, \
			GROUP_ITEM_GROUP_COUNT, g_groups.count);
	if (write(fd, buff, len) != len)
	{
		logError("file: "__FILE__", line: %d, " \
			"write to file \"%s\" fail, " \
			"errno: %d, error info: %s", \
			__LINE__, tmpFilename, \
			errno, STRERROR(errno));
		result = errno != 0 ? errno : EIO;
	}
	else
	{
	result = 0;

	ppEnd = g_groups.sorted_groups + g_groups.count;
	for (ppGroup=g_groups.sorted_groups; ppGroup<ppEnd; ppGroup++)
	{
		len = sprintf(buff, \
				"# group: %s\n" \
				"[%s"GROUP_SECTION_NO_FORMAT"]\n" \
				"\t%s=%s\n" \
				"\t%s=%d\n" \
				"\t%s=%d\n" \
				"\t%s=%d\n" \
				"\t%s=%d\n" \
				"\t%s=%d\n\n", \
				(*ppGroup)->group_name, \
				GROUP_SECTION_NAME_PREFIX, \
				(int)(ppGroup - g_groups.sorted_groups) + 1, \
				GROUP_ITEM_GROUP_NAME, \
				(*ppGroup)->group_name, \
				GROUP_ITEM_STORAGE_PORT, \
				(*ppGroup)->storage_port, \
				GROUP_ITEM_STORAGE_HTTP_PORT, \
				(*ppGroup)->storage_http_port, \
				GROUP_ITEM_STORE_PATH_COUNT, \
				(*ppGroup)->store_path_count, \
				GROUP_ITEM_SUBDIR_COUNT_PER_PATH, \
				(*ppGroup)->subdir_count_per_path, \
				GROUP_ITEM_CURRENT_TRUNK_FILE_ID, \
				(*ppGroup)->current_trunk_file_id);

		if (write(fd, buff, len) != len)
		{
			logError("file: "__FILE__", line: %d, " \
				"write to file \"%s\" fail, " \
				"errno: %d, error info: %s", \
				__LINE__, tmpFilename, errno, STRERROR(errno));
			result = errno != 0 ? errno : EIO;
			break;
		}
	}
	}

	if (result == 0)
	{
		if (fsync(fd) != 0)
		{
			logError("file: "__FILE__", line: %d, " \
				"fsync file \"%s\" fail, " \
				"errno: %d, error info: %s", \
				__LINE__, tmpFilename, \
				errno, STRERROR(errno));
			result = errno != 0 ? errno : EIO;
		}
	}

	close(fd);

	if (result == 0)
	{
		if (rename(tmpFilename, trueFilename) != 0)
		{
			logError("file: "__FILE__", line: %d, " \
				"rename file \"%s\" to \"%s\" fail, " \
				"errno: %d, error info: %s", \
				__LINE__, tmpFilename, trueFilename, \
				errno, STRERROR(errno));
			result = errno != 0 ? errno : EIO;
		}

		TRACKER_CHOWN(trueFilename, geteuid(), getegid())
	}

	if (result != 0)
	{
		unlink(tmpFilename);
	}

	tracker_mem_file_unlock();

	return result;
}

int tracker_save_storages()
{
	char tmpFilename[MAX_PATH_SIZE];
	char trueFilename[MAX_PATH_SIZE];
	char buff[4096];
	int fd;
	int len;
	FDFSGroupInfo **ppGroup;
	FDFSGroupInfo **ppGroupEnd;
	FDFSStorageDetail **ppStorage;
	FDFSStorageDetail **ppStorageEnd;
	FDFSStorageDetail *pStorage;
	int result;
	int count;

	tracker_mem_file_lock();

	snprintf(trueFilename, sizeof(trueFilename), "%s/data/%s", \
		g_fdfs_base_path, STORAGE_SERVERS_LIST_FILENAME_NEW);
	snprintf(tmpFilename, sizeof(tmpFilename), "%s.tmp", trueFilename);
	if ((fd=open(tmpFilename, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
	{
		tracker_mem_file_unlock();

		logError("file: "__FILE__", line: %d, " \
			"open \"%s\" fail, " \
			"errno: %d, error info: %s", \
			__LINE__, tmpFilename, errno, STRERROR(errno));
		return errno != 0 ? errno : ENOENT;
	}

	count = 0;
	result = 0;
	ppGroupEnd = g_groups.sorted_groups + g_groups.count;
	for (ppGroup=g_groups.sorted_groups; \
		(ppGroup < ppGroupEnd) && (result == 0); ppGroup++)
	{
		ppStorageEnd = (*ppGroup)->all_servers + (*ppGroup)->count;
		for (ppStorage=(*ppGroup)->all_servers; \
			ppStorage<ppStorageEnd; ppStorage++)
		{
			pStorage = *ppStorage;
			if (pStorage->status == FDFS_STORAGE_STATUS_DELETED
			 || pStorage->status == FDFS_STORAGE_STATUS_IP_CHANGED)
			{
				continue;
			}

			count++;
			len = sprintf(buff, \
				"# storage %s:%d\n" \
				"[%s"STORAGE_SECTION_NO_FORMAT"]\n" \
				"\t%s=%s\n" \
				"\t%s=%s\n" \
				"\t%s=%d\n" \
				"\t%s=%s\n" \
				"\t%s=%d\n" \
				"\t%s=%d\n" \
				"\t%s=%d\n" \
				"\t%s=%s\n" \
				"\t%s=%s\n" \
				"\t%s=%d\n" \
				"\t%s=%d\n" \
				"\t%s=%d\n" \
				"\t%s=%d\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n" \
				"\t%s=%d\n" \
				"\t%s=%d\n" \
				"\t%s=%d\n" \
				"\t%s=%d\n" \
				"\t%s="INT64_PRINTF_FORMAT"\n\n", \
				pStorage->ip_addr, pStorage->storage_port, \
				STORAGE_SECTION_NAME_PREFIX, count, \
				STORAGE_ITEM_GROUP_NAME, \
				(*ppGroup)->group_name, \
				STORAGE_ITEM_IP_ADDR, pStorage->ip_addr, \
				STORAGE_ITEM_STATUS, pStorage->status, \
				STORAGE_ITEM_VERSION, pStorage->version, \
				STORAGE_ITEM_JOIN_TIME, \
				(int)pStorage->join_time, \
				STORAGE_ITEM_STORAGE_PORT, \
				pStorage->storage_port, \
				STORAGE_ITEM_STORAGE_HTTP_PORT, \
				pStorage->storage_http_port,  \
				STORAGE_ITEM_DOMAIN_NAME, \
				pStorage->domain_name, \
				STORAGE_ITEM_SYNC_SRC_SERVER, \
				(pStorage->psync_src_server != NULL ? \
				pStorage->psync_src_server->ip_addr : ""), \
				STORAGE_ITEM_SYNC_UNTIL_TIMESTAMP, \
				(int)pStorage->sync_until_timestamp, \
				STORAGE_ITEM_STORE_PATH_COUNT, \
				pStorage->store_path_count, \
				STORAGE_ITEM_SUBDIR_COUNT_PER_PATH, \
				pStorage->subdir_count_per_path, \
				STORAGE_ITEM_UPLOAD_PRIORITY, \
				pStorage->upload_priority, \
				STORAGE_ITEM_TOTAL_MB, pStorage->total_mb, \
				STORAGE_ITEM_FREE_MB, pStorage->free_mb, \
				STORAGE_ITEM_TOTAL_UPLOAD_COUNT, \
				pStorage->stat.total_upload_count, \
				STORAGE_ITEM_SUCCESS_UPLOAD_COUNT, \
				pStorage->stat.success_upload_count, \
				STORAGE_ITEM_TOTAL_APPEND_COUNT, \
				pStorage->stat.total_append_count, \
				STORAGE_ITEM_SUCCESS_APPEND_COUNT, \
				pStorage->stat.success_append_count, \
				STORAGE_ITEM_TOTAL_SET_META_COUNT, \
				pStorage->stat.total_set_meta_count, \
				STORAGE_ITEM_SUCCESS_SET_META_COUNT, \
				pStorage->stat.success_set_meta_count, \
				STORAGE_ITEM_TOTAL_DELETE_COUNT, \
				pStorage->stat.total_delete_count, \
				STORAGE_ITEM_SUCCESS_DELETE_COUNT, \
				pStorage->stat.success_delete_count, \
				STORAGE_ITEM_TOTAL_DOWNLOAD_COUNT, \
				pStorage->stat.total_download_count, \
				STORAGE_ITEM_SUCCESS_DOWNLOAD_COUNT, \
				pStorage->stat.success_download_count, \
				STORAGE_ITEM_TOTAL_GET_META_COUNT, \
				pStorage->stat.total_get_meta_count, \
				STORAGE_ITEM_SUCCESS_GET_META_COUNT, \
				pStorage->stat.success_get_meta_count, \
				STORAGE_ITEM_TOTAL_CREATE_LINK_COUNT, \
				pStorage->stat.total_create_link_count, \
				STORAGE_ITEM_SUCCESS_CREATE_LINK_COUNT, \
				pStorage->stat.success_create_link_count, \
				STORAGE_ITEM_TOTAL_DELETE_LINK_COUNT, \
				pStorage->stat.total_delete_link_count, \
				STORAGE_ITEM_SUCCESS_DELETE_LINK_COUNT, \
				pStorage->stat.success_delete_link_count, \
				STORAGE_ITEM_TOTAL_UPLOAD_BYTES, \
				pStorage->stat.total_upload_bytes, \
				STORAGE_ITEM_SUCCESS_UPLOAD_BYTES, \
				pStorage->stat.success_upload_bytes, \
				STORAGE_ITEM_TOTAL_APPEND_BYTES, \
				pStorage->stat.total_append_bytes, \
				STORAGE_ITEM_SUCCESS_APPEND_BYTES, \
				pStorage->stat.success_append_bytes, \
				STORAGE_ITEM_TOTAL_DOWNLOAD_BYTES, \
				pStorage->stat.total_download_bytes, \
				STORAGE_ITEM_SUCCESS_DOWNLOAD_BYTES, \
				pStorage->stat.success_download_bytes, \
				STORAGE_ITEM_TOTAL_SYNC_IN_BYTES, \
				pStorage->stat.total_sync_in_bytes, \
				STORAGE_ITEM_SUCCESS_SYNC_IN_BYTES, \
				pStorage->stat.success_sync_in_bytes, \
				STORAGE_ITEM_TOTAL_SYNC_OUT_BYTES, \
				pStorage->stat.total_sync_out_bytes, \
				STORAGE_ITEM_SUCCESS_SYNC_OUT_BYTES, \
				pStorage->stat.success_sync_out_bytes, \
				STORAGE_ITEM_TOTAL_FILE_OPEN_COUNT, \
				pStorage->stat.total_file_open_count, \
				STORAGE_ITEM_SUCCESS_FILE_OPEN_COUNT, \
				pStorage->stat.success_file_open_count, \
				STORAGE_ITEM_TOTAL_FILE_READ_COUNT, \
				pStorage->stat.total_file_read_count, \
				STORAGE_ITEM_SUCCESS_FILE_READ_COUNT, \
				pStorage->stat.success_file_read_count, \
				STORAGE_ITEM_TOTAL_FILE_WRITE_COUNT, \
				pStorage->stat.total_file_write_count, \
				STORAGE_ITEM_SUCCESS_FILE_WRITE_COUNT, \
				pStorage->stat.success_file_write_count, \
				STORAGE_ITEM_LAST_SOURCE_UPDATE, \
				(int)(pStorage->stat.last_source_update), \
				STORAGE_ITEM_LAST_SYNC_UPDATE, \
				(int)(pStorage->stat.last_sync_update), \
				STORAGE_ITEM_LAST_SYNCED_TIMESTAMP, \
				(int)pStorage->stat.last_synced_timestamp, \
				STORAGE_ITEM_LAST_HEART_BEAT_TIME, \
				(int)pStorage->stat.last_heart_beat_time, \
				STORAGE_ITEM_CHANGELOG_OFFSET, \
				pStorage->changelog_offset \
	 		     );

			if (write(fd, buff, len) != len)
			{
				logError("file: "__FILE__", line: %d, " \
					"write to file \"%s\" fail, " \
					"errno: %d, error info: %s", \
					__LINE__, tmpFilename, \
					errno, STRERROR(errno));
				result = errno != 0 ? errno : EIO;
				break;
			}
		}
	}

	if (result == 0)
	{
		len = sprintf(buff, \
			"\n# global section\n" \
			"[%s]\n" \
			"\t%s=%d\n", \
			STORAGE_SECTION_NAME_GLOBAL, \
			STORAGE_ITEM_STORAGE_COUNT, count);
		if (write(fd, buff, len) != len)
		{
			logError("file: "__FILE__", line: %d, " \
				"write to file \"%s\" fail, " \
				"errno: %d, error info: %s", \
				__LINE__, tmpFilename, \
				errno, STRERROR(errno));
			result = errno != 0 ? errno : EIO;
		}
	}

	if (result == 0)
	{
		if (fsync(fd) != 0)
		{
			logError("file: "__FILE__", line: %d, " \
				"fsync file \"%s\" fail, " \
				"errno: %d, error info: %s", \
				__LINE__, tmpFilename, \
				errno, STRERROR(errno));
			result = errno != 0 ? errno : EIO;
		}
	}

	close(fd);

	if (result == 0)
	{
		if (rename(tmpFilename, trueFilename) != 0)
		{
			logError("file: "__FILE__", line: %d, " \
				"rename file \"%s\" to \"%s\" fail, " \
				"errno: %d, error info: %s", \
				__LINE__, tmpFilename, trueFilename, \
				errno, STRERROR(errno));
			result = errno != 0 ? errno : EIO;
		}

		TRACKER_CHOWN(trueFilename, geteuid(), getegid())
	}

	if (result != 0)
	{
		unlink(tmpFilename);
	}

	tracker_mem_file_unlock();

	return result;
}

int tracker_save_sync_timestamps()
{
	char tmpFilename[MAX_PATH_SIZE];
	char trueFilename[MAX_PATH_SIZE];
	char buff[512];
	int fd;
	int len;
	FDFSGroupInfo **ppGroup;
	FDFSGroupInfo **ppGroupEnd;
	int **last_sync_timestamps;
	int i;
	int k;
	int result;

	tracker_mem_file_lock();

	snprintf(trueFilename, sizeof(trueFilename), "%s/data/%s", \
		g_fdfs_base_path, STORAGE_SYNC_TIMESTAMP_FILENAME);
	snprintf(tmpFilename, sizeof(tmpFilename), "%s.tmp", trueFilename);
	if ((fd=open(tmpFilename, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
	{
		tracker_mem_file_unlock();

		logError("file: "__FILE__", line: %d, " \
			"open \"%s\" fail, " \
			"errno: %d, error info: %s", \
			__LINE__, tmpFilename, errno, STRERROR(errno));
		return errno != 0 ? errno : ENOENT;
	}

	result = 0;
	ppGroupEnd = g_groups.sorted_groups + g_groups.count;
	for (ppGroup=g_groups.sorted_groups; \
		(ppGroup < ppGroupEnd) && (result == 0); ppGroup++)
	{
		last_sync_timestamps = (*ppGroup)->last_sync_timestamps;
		for (i=0; i<(*ppGroup)->count; i++)
		{
			if ((*ppGroup)->all_servers[i]->status == \
				FDFS_STORAGE_STATUS_DELETED \
			 || (*ppGroup)->all_servers[i]->status == \
				FDFS_STORAGE_STATUS_IP_CHANGED)
			{
				continue;
			}

			len = sprintf(buff, "%s%c%s", (*ppGroup)->group_name, \
				STORAGE_DATA_FIELD_SEPERATOR, \
				(*ppGroup)->all_servers[i]->ip_addr);
			for (k=0; k<(*ppGroup)->count; k++)
			{
				if ((*ppGroup)->all_servers[k]->status == \
					FDFS_STORAGE_STATUS_DELETED \
				 || (*ppGroup)->all_servers[k]->status == \
					FDFS_STORAGE_STATUS_IP_CHANGED)
				{
					continue;
				}

				len += sprintf(buff + len, "%c%d", \
					STORAGE_DATA_FIELD_SEPERATOR, \
					last_sync_timestamps[i][k]);
			}
			*(buff + len) = '\n';
			len++;

			if (write(fd, buff, len) != len)
			{
				logError("file: "__FILE__", line: %d, " \
					"write to file \"%s\" fail, " \
					"errno: %d, error info: %s", \
					__LINE__, tmpFilename, \
					errno, STRERROR(errno));
				result = errno != 0 ? errno : EIO;
				break;
			}
		}
	}

	if (result == 0)
	{
		if (fsync(fd) != 0)
		{
			logError("file: "__FILE__", line: %d, " \
				"fsync file \"%s\" fail, " \
				"errno: %d, error info: %s", \
				__LINE__, tmpFilename, \
				errno, STRERROR(errno));
			result = errno != 0 ? errno : EIO;
		}
	}

	close(fd);

	if (result == 0)
	{
		if (rename(tmpFilename, trueFilename) != 0)
		{
			logError("file: "__FILE__", line: %d, " \
				"rename file \"%s\" to \"%s\" fail, " \
				"errno: %d, error info: %s", \
				__LINE__, tmpFilename, trueFilename, \
				errno, STRERROR(errno));
			result = errno != 0 ? errno : EIO;
		}

		TRACKER_CHOWN(trueFilename, geteuid(), getegid())
	}

	if (result != 0)
	{
		unlink(tmpFilename);
	}

	tracker_mem_file_unlock();

	return result;
}

int tracker_save_sys_files()
{
	int result;

	if ((result=tracker_save_groups()) != 0)
	{
		return result;
	}

	if ((result=tracker_save_storages()) != 0)
	{
		return result;
	}

	return tracker_save_sync_timestamps();
}

static int tracker_open_changlog_file()
{
	char data_path[MAX_PATH_SIZE];
	char filename[MAX_PATH_SIZE];

	snprintf(data_path, sizeof(data_path), "%s/data", g_fdfs_base_path);
	if (!fileExists(data_path))
	{
		if (mkdir(data_path, 0755) != 0)
		{
			logError("file: "__FILE__", line: %d, " \
				"mkdir \"%s\" fail, " \
				"errno: %d, error info: %s", \
				__LINE__, data_path, errno, STRERROR(errno));
			return errno != 0 ? errno : ENOENT;
		}
		TRACKER_CHOWN(data_path, geteuid(), getegid())
	}

	snprintf(filename, sizeof(filename), "%s/data/%s", \
		g_fdfs_base_path, STORAGE_SERVERS_CHANGELOG_FILENAME);
	changelog_fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (changelog_fd < 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"open \"%s\" fail, " \
			"errno: %d, error info: %s", \
			__LINE__, filename, errno, STRERROR(errno));
		return errno != 0 ? errno : ENOENT;
	}

	g_changelog_fsize = lseek(changelog_fd, 0, SEEK_END);
        if (g_changelog_fsize < 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"lseek file \"%s\" fail, " \
			"errno: %d, error info: %s", \
			__LINE__, filename, errno, STRERROR(errno));
		return errno != 0 ? errno : EIO;
	}

	TRACKER_FCHOWN(changelog_fd, filename, geteuid(), getegid())

	return 0;
}

static int tracker_mem_init_groups(FDFSGroups *pGroups)
{
	int result;
	FDFSGroupInfo **ppGroup;
	FDFSGroupInfo **ppGroupEnd;

	pGroups->alloc_size = TRACKER_MEM_ALLOC_ONCE;
	pGroups->count = 0;
	pGroups->current_write_group = 0;
	pGroups->pStoreGroup = NULL;
	pGroups->groups = (FDFSGroupInfo **)malloc( \
			sizeof(FDFSGroupInfo *) * pGroups->alloc_size);
	if (pGroups->groups == NULL)
	{
		logCrit("file: "__FILE__", line: %d, " \
			"malloc %d bytes fail!", __LINE__, \
			(int)sizeof(FDFSGroupInfo *) * pGroups->alloc_size);
		return errno != 0 ? errno : ENOMEM;
	}

	memset(pGroups->groups, 0, \
		sizeof(FDFSGroupInfo *) * pGroups->alloc_size);

	ppGroupEnd = pGroups->groups + pGroups->alloc_size;
	for (ppGroup=pGroups->groups; ppGroup<ppGroupEnd; ppGroup++)
	{
		*ppGroup = (FDFSGroupInfo *)malloc(sizeof(FDFSGroupInfo));
		if (*ppGroup == NULL)
		{
			logCrit("file: "__FILE__", line: %d, " \
				"malloc %d bytes fail!", \
				__LINE__, (int)sizeof(FDFSGroupInfo));
			return errno != 0 ? errno : ENOMEM;
		}

		memset(*ppGroup, 0, sizeof(FDFSGroupInfo));
	}

	pGroups->sorted_groups = (FDFSGroupInfo **) \
			malloc(sizeof(FDFSGroupInfo *) * pGroups->alloc_size);
	if (pGroups->sorted_groups == NULL)
	{
		free(pGroups->groups);
		pGroups->groups = NULL;

		logCrit("file: "__FILE__", line: %d, " \
			"malloc %d bytes fail!", __LINE__, \
			(int)sizeof(FDFSGroupInfo *) * pGroups->alloc_size);
		return errno != 0 ? errno : ENOMEM;
	}

	memset(pGroups->sorted_groups, 0, \
		sizeof(FDFSGroupInfo *) * pGroups->alloc_size);

	if ((result=tracker_load_data(pGroups)) != 0)
	{
		return result;
	}

	return 0;
}

int tracker_mem_init()
{
	int result;

	if ((result=init_pthread_lock(&mem_thread_lock)) != 0)
	{
		return result;
	}

	if ((result=init_pthread_lock(&mem_file_lock)) != 0)
	{
		return result;
	}

	if ((result=tracker_open_changlog_file()) != 0)
	{
		return result;
	}

	return tracker_mem_init_groups(&g_groups);
}

static void tracker_free_last_sync_timestamps(int **last_sync_timestamps, \
		const int alloc_size)
{
	int i;

	if (last_sync_timestamps != NULL)
	{
		for (i=0; i<alloc_size; i++)
		{
			if (last_sync_timestamps[i] != NULL)
			{
				free(last_sync_timestamps[i]);
				last_sync_timestamps[i] = NULL;
			}
		}

		free(last_sync_timestamps);
	}
}

static int **tracker_malloc_last_sync_timestamps(const int alloc_size, \
		int *err_no)
{
	int **results;
	int i;

	results = (int **)malloc(sizeof(int *) * alloc_size);
	if (results == NULL)
	{
		*err_no = errno != 0 ? errno : ENOMEM;
		logError("file: "__FILE__", line: %d, " \
			"malloc %d bytes fail", __LINE__, \
			(int)sizeof(int *) * alloc_size);
		return NULL;
	}

	memset(results, 0, sizeof(int *) * alloc_size);
	for (i=0; i<alloc_size; i++)
	{
		results[i] = (int *)malloc(sizeof(int) * alloc_size);
		if (results[i] == NULL)
		{
			*err_no = errno != 0 ? errno : ENOMEM;
			logError("file: "__FILE__", line: %d, " \
				"malloc %d bytes fail", __LINE__, \
				(int)sizeof(int) * alloc_size);

			tracker_free_last_sync_timestamps(results, alloc_size);
			return NULL;
		}

		memset(results[i], 0, sizeof(int) * alloc_size);
	}

	*err_no = 0;
	return results;
}

static void tracker_mem_free_storages(FDFSStorageDetail **servers, const int count)
{
	FDFSStorageDetail **ppServer;
	FDFSStorageDetail **ppServerEnd;

	ppServerEnd = servers + count;
	for (ppServer=servers; ppServer<ppServerEnd; ppServer++)
	{
		if (*ppServer != NULL)
		{
			free(*ppServer);
		}
	}

	free(servers);
}

static void tracker_mem_free_group(FDFSGroupInfo *pGroup)
{
	if (pGroup->sorted_servers != NULL)
	{
		free(pGroup->sorted_servers);
		pGroup->sorted_servers = NULL;
	}

	if (pGroup->active_servers != NULL)
	{
		free(pGroup->active_servers);
		pGroup->active_servers = NULL;
	}

	if (pGroup->all_servers != NULL)
	{
		tracker_mem_free_storages(pGroup->all_servers, \
				pGroup->alloc_size);
		pGroup->all_servers = NULL;
	}

#ifdef WITH_HTTPD
	if (g_http_check_interval > 0)
	{
		if (pGroup->http_servers != NULL)
		{
			free(pGroup->http_servers);
			pGroup->http_servers = NULL;
		}
	}
#endif

	tracker_free_last_sync_timestamps(pGroup->last_sync_timestamps, \
				pGroup->alloc_size);
	pGroup->last_sync_timestamps = NULL;
}

static int tracker_mem_init_group(FDFSGroupInfo *pGroup)
{
	FDFSStorageDetail **ppServer;
	FDFSStorageDetail **ppServerEnd;
	int err_no;

	pGroup->alloc_size = TRACKER_MEM_ALLOC_ONCE;
	pGroup->count = 0;
	pGroup->all_servers = (FDFSStorageDetail **) \
			malloc(sizeof(FDFSStorageDetail *) * pGroup->alloc_size);
	if (pGroup->all_servers == NULL)
	{
		logError("file: "__FILE__", line: %d, " \
			"malloc %d bytes fail", __LINE__, \
			(int)sizeof(FDFSStorageDetail *) * pGroup->alloc_size);
		return errno != 0 ? errno : ENOMEM;
	}

	memset(pGroup->all_servers, 0, \
		sizeof(FDFSStorageDetail *) * pGroup->alloc_size);
	ppServerEnd = pGroup->all_servers + pGroup->alloc_size;	
	for (ppServer=pGroup->all_servers; ppServer<ppServerEnd; ppServer++)
	{
		*ppServer = (FDFSStorageDetail *)malloc( \
					sizeof(FDFSStorageDetail));
		if (*ppServer == NULL)
		{
			tracker_mem_free_group(pGroup);

			logError("file: "__FILE__", line: %d, " \
				"malloc %d bytes fail", __LINE__, \
				(int)sizeof(FDFSStorageDetail));
			return errno != 0 ? errno : ENOMEM;
		}

		memset(*ppServer, 0, sizeof(FDFSStorageDetail));
	}

	pGroup->sorted_servers = (FDFSStorageDetail **) \
		malloc(sizeof(FDFSStorageDetail *) * pGroup->alloc_size);
	if (pGroup->sorted_servers == NULL)
	{
		tracker_mem_free_group(pGroup);

		logError("file: "__FILE__", line: %d, " \
			"malloc %d bytes fail", __LINE__, \
			(int)sizeof(FDFSStorageDetail *) * pGroup->alloc_size);
		return errno != 0 ? errno : ENOMEM;
	}
	memset(pGroup->sorted_servers, 0, \
		sizeof(FDFSStorageDetail *) * pGroup->alloc_size);

	pGroup->active_servers = (FDFSStorageDetail **) \
		malloc(sizeof(FDFSStorageDetail *) * pGroup->alloc_size);
	if (pGroup->active_servers == NULL)
	{
		tracker_mem_free_group(pGroup);

		logError("file: "__FILE__", line: %d, " \
			"malloc %d bytes fail", __LINE__, \
			(int)sizeof(FDFSStorageDetail *) * pGroup->alloc_size);
		return errno != 0 ? errno : ENOMEM;
	}
	memset(pGroup->active_servers, 0, \
		sizeof(FDFSStorageDetail *) * pGroup->alloc_size);

#ifdef WITH_HTTPD
	if (g_http_check_interval <= 0)
	{
		pGroup->http_servers = pGroup->active_servers;
	}
	else
	{
		pGroup->http_servers = (FDFSStorageDetail **) \
			malloc(sizeof(FDFSStorageDetail *)*pGroup->alloc_size);
		if (pGroup->http_servers == NULL)
		{
			tracker_mem_free_group(pGroup);

			logError("file: "__FILE__", line: %d, " \
				"malloc %d bytes fail", __LINE__, \
				(int)sizeof(FDFSStorageDetail *) * \
				pGroup->alloc_size);
			return errno != 0 ? errno : ENOMEM;
		}
		memset(pGroup->http_servers, 0, \
			sizeof(FDFSStorageDetail *) * pGroup->alloc_size);
		g_http_servers_dirty = true;
	}
#endif

	pGroup->last_sync_timestamps = tracker_malloc_last_sync_timestamps( \
			pGroup->alloc_size, &err_no);
	return err_no;
}

static int tracker_mem_destroy_groups(FDFSGroups *pGroups, const bool saveFiles)
{
	FDFSGroupInfo **ppGroup;
	FDFSGroupInfo **ppEnd;
	int result;

	if (pGroups->groups == NULL)
	{
		result = 0;
	}
	else
	{
		if (saveFiles)
		{
			result = tracker_save_sys_files();
		}
		else
		{
			result = 0;
		}

		ppEnd = pGroups->groups + pGroups->count;
		for (ppGroup=pGroups->groups; ppGroup<ppEnd; ppGroup++)
		{
			tracker_mem_free_group(*ppGroup);
		}

		if (pGroups->sorted_groups != NULL)
		{
			free(pGroups->sorted_groups);
			pGroups->sorted_groups = NULL;
		}

		free(pGroups->groups);
		pGroups->groups = NULL;
	}

	return result;
}

int tracker_mem_destroy()
{
	int result;

	result = tracker_mem_destroy_groups(&g_groups, true);

	if (changelog_fd >= 0)
	{
		close(changelog_fd);
		changelog_fd = -1;
	}

	if (pthread_mutex_destroy(&mem_thread_lock) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call pthread_mutex_destroy fail", \
			__LINE__);
	}

	if (pthread_mutex_destroy(&mem_file_lock) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call pthread_mutex_destroy fail", \
			__LINE__);
	}

	return result;
}

static void tracker_mem_free_groups(FDFSGroupInfo **groups, const int count)
{
	FDFSGroupInfo **ppGroup;
	FDFSGroupInfo **ppGroupEnd;

	ppGroupEnd = groups + count;
	for (ppGroup=groups; ppGroup<ppGroupEnd; ppGroup++)
	{
		if (*ppGroup != NULL)
		{
			free(*ppGroup);
		}
	}

	free(groups);
}

static int tracker_mem_realloc_groups(FDFSGroups *pGroups, const bool bNeedSleep)
{
	FDFSGroupInfo **old_groups;
	FDFSGroupInfo **old_sorted_groups;
	FDFSGroupInfo **new_groups;
	FDFSGroupInfo **new_sorted_groups;
	FDFSGroupInfo **ppGroup;
	FDFSGroupInfo **ppGroupEnd;
	int new_size;

	new_size = pGroups->alloc_size + TRACKER_MEM_ALLOC_ONCE;
	new_groups = (FDFSGroupInfo **)malloc(sizeof(FDFSGroupInfo *) * new_size);
	if (new_groups == NULL)
	{
		logError("file: "__FILE__", line: %d, " \
			"malloc %d bytes fail", \
			__LINE__, (int)sizeof(FDFSGroupInfo *) * new_size);
		return errno != 0 ? errno : ENOMEM;
	}
	memset(new_groups, 0, sizeof(FDFSGroupInfo *) * new_size);

	ppGroupEnd = new_groups + new_size;
	for (ppGroup=new_groups+pGroups->count; ppGroup<ppGroupEnd; ppGroup++)
	{
		*ppGroup = (FDFSGroupInfo *)malloc(sizeof(FDFSGroupInfo));
		if (*ppGroup == NULL)
		{
			tracker_mem_free_groups(new_groups, new_size);

			logCrit("file: "__FILE__", line: %d, " \
				"malloc %d bytes fail", \
				__LINE__, (int)sizeof(FDFSGroupInfo));
			return errno != 0 ? errno : ENOMEM;
		}

		memset(*ppGroup, 0, sizeof(FDFSGroupInfo));
	}

	memcpy(new_groups, pGroups->groups, \
		sizeof(FDFSGroupInfo *) * pGroups->count);

	new_sorted_groups = (FDFSGroupInfo **)malloc( \
			sizeof(FDFSGroupInfo *) * new_size);
	if (new_sorted_groups == NULL)
	{
		tracker_mem_free_groups(new_groups, new_size);

		logError("file: "__FILE__", line: %d, " \
			"malloc %d bytes fail", \
			__LINE__, (int)sizeof(FDFSGroupInfo *) * new_size);
		return errno != 0 ? errno : ENOMEM;
	}

	memset(new_sorted_groups, 0, sizeof(FDFSGroupInfo *) * new_size);
	memcpy(new_sorted_groups, pGroups->sorted_groups, \
		sizeof(FDFSGroupInfo *) * pGroups->count);

	old_groups = pGroups->groups;
	old_sorted_groups = pGroups->sorted_groups;
	pGroups->alloc_size = new_size;
	pGroups->groups = new_groups;
	pGroups->sorted_groups = new_sorted_groups;

	if (bNeedSleep)
	{
		sleep(1);
	}

	free(old_groups);
	free(old_sorted_groups);

	return 0;
}

int tracker_get_group_file_count(FDFSGroupInfo *pGroup)
{
	int count;
	FDFSStorageDetail **ppServer;
	FDFSStorageDetail **ppServerEnd;

	count = 0;
	ppServerEnd = pGroup->all_servers + pGroup->count;
	for (ppServer=pGroup->all_servers; ppServer<ppServerEnd; ppServer++)
	{
		count += (*ppServer)->stat.success_upload_count - \
				(*ppServer)->stat.success_delete_count;
	}

	return count;
}

int tracker_get_group_success_upload_count(FDFSGroupInfo *pGroup)
{
	int count;
	FDFSStorageDetail **ppServer;
	FDFSStorageDetail **ppServerEnd;

	count = 0;
	ppServerEnd = pGroup->all_servers + pGroup->count;
	for (ppServer=pGroup->all_servers; ppServer<ppServerEnd; ppServer++)
	{
		count += (*ppServer)->stat.success_upload_count;
	}

	return count;
}

FDFSStorageDetail *tracker_get_group_sync_src_server(FDFSGroupInfo *pGroup, \
			FDFSStorageDetail *pDestServer)
{
	FDFSStorageDetail **ppServer;
	FDFSStorageDetail **ppServerEnd;

	ppServerEnd = pGroup->active_servers + pGroup->active_count;
	for (ppServer=pGroup->active_servers; ppServer<ppServerEnd; ppServer++)
	{
		if (strcmp((*ppServer)->ip_addr, pDestServer->ip_addr) == 0)
		{
			continue;
		}

		return *ppServer;
	}

	return NULL;
}

static int tracker_mem_realloc_store_servers(FDFSGroupInfo *pGroup, \
		const int inc_count, const bool bNeedSleep)
{
	int result;
	FDFSStorageDetail **old_servers;
	FDFSStorageDetail **old_sorted_servers;
	FDFSStorageDetail **old_active_servers;
	int **old_last_sync_timestamps;
	FDFSStorageDetail **new_servers;
	FDFSStorageDetail **new_sorted_servers;
	FDFSStorageDetail **new_active_servers;
	FDFSStorageDetail **ppServer;
	FDFSStorageDetail **ppServerEnd;
#ifdef WITH_HTTPD
	FDFSStorageDetail **old_http_servers;
	FDFSStorageDetail **new_http_servers;
#endif
	int **new_last_sync_timestamps;
	int old_size;
	int new_size;
	int err_no;
	int i;
	
	new_size = pGroup->alloc_size + inc_count + TRACKER_MEM_ALLOC_ONCE;
	new_servers = (FDFSStorageDetail **) \
		malloc(sizeof(FDFSStorageDetail *) * new_size);
	if (new_servers == NULL)
	{
		logError("file: "__FILE__", line: %d, " \
			"malloc %d bytes fail", \
			__LINE__, (int)sizeof(FDFSStorageDetail *) * new_size);
		return errno != 0 ? errno : ENOMEM;
	}
	memset(new_servers, 0, sizeof(FDFSStorageDetail *) * new_size);

	ppServerEnd = new_servers + new_size;	
	for (ppServer=new_servers+pGroup->count; ppServer<ppServerEnd; ppServer++)
	{
		*ppServer = (FDFSStorageDetail *)malloc( \
					sizeof(FDFSStorageDetail));
		if (*ppServer == NULL)
		{
			tracker_mem_free_storages(new_servers, new_size);

			logError("file: "__FILE__", line: %d, " \
				"malloc %d bytes fail", __LINE__, \
				(int)sizeof(FDFSStorageDetail));
			return errno != 0 ? errno : ENOMEM;
		}

		memset(*ppServer, 0, sizeof(FDFSStorageDetail));
	}

	memcpy(new_servers, pGroup->all_servers, \
		sizeof(FDFSStorageDetail *) * pGroup->count);

	new_sorted_servers = (FDFSStorageDetail **) \
		malloc(sizeof(FDFSStorageDetail *) * new_size);
	if (new_sorted_servers == NULL)
	{
		free(new_servers);
		logError("file: "__FILE__", line: %d, " \
			"malloc %d bytes fail", \
			__LINE__, (int)sizeof(FDFSStorageDetail *) * new_size);
		return errno != 0 ? errno : ENOMEM;
	}

	new_active_servers = (FDFSStorageDetail **) \
		malloc(sizeof(FDFSStorageDetail *) * new_size);
	if (new_active_servers == NULL)
	{
		free(new_servers);
		free(new_sorted_servers);

		logError("file: "__FILE__", line: %d, " \
			"malloc %d bytes fail", \
			__LINE__, (int)sizeof(FDFSStorageDetail *) * new_size);
		return errno != 0 ? errno : ENOMEM;
	}

#ifdef WITH_HTTPD
	if (g_http_check_interval > 0)
	{
		new_http_servers = (FDFSStorageDetail **) \
			malloc(sizeof(FDFSStorageDetail *) * new_size);
		if (new_http_servers == NULL)
		{
			free(new_servers);
			free(new_sorted_servers);
			free(new_active_servers);

			logError("file: "__FILE__", line: %d, " \
				"malloc %d bytes fail", __LINE__, \
				(int)sizeof(FDFSStorageDetail *) * new_size);
			return errno != 0 ? errno : ENOMEM;
		}

		memset(new_http_servers,0,sizeof(FDFSStorageDetail *)*new_size);

		memcpy(new_http_servers, pGroup->http_servers, \
			sizeof(FDFSStorageDetail *) * pGroup->count);

	}
	else
	{
		new_http_servers = NULL;
	}
#endif

	memset(new_sorted_servers, 0, sizeof(FDFSStorageDetail *) * new_size);
	memset(new_active_servers, 0, sizeof(FDFSStorageDetail *) * new_size);
	if (pGroup->store_path_count > 0)
	{
		for (i=pGroup->count; i<new_size; i++)
		{
			result=tracker_malloc_storage_path_mbs(*(new_servers+i), \
				pGroup->store_path_count);
			if (result != 0)
			{
				free(new_servers);
				free(new_sorted_servers);

				return result;
			}
		}
	}

	memcpy(new_sorted_servers, pGroup->sorted_servers, \
		sizeof(FDFSStorageDetail *) * pGroup->count);

	memcpy(new_active_servers, pGroup->active_servers, \
		sizeof(FDFSStorageDetail *) * pGroup->count);

	new_last_sync_timestamps = tracker_malloc_last_sync_timestamps( \
		new_size, &err_no);
	if (new_last_sync_timestamps == NULL)
	{
		free(new_servers);
		free(new_sorted_servers);
		free(new_active_servers);

		return err_no;
	}
	for (i=0; i<pGroup->alloc_size; i++)
	{
		memcpy(new_last_sync_timestamps[i],  \
			pGroup->last_sync_timestamps[i], \
			(int)sizeof(int) *  pGroup->alloc_size);
	}

	old_size = pGroup->alloc_size;
	old_servers = pGroup->all_servers;
	old_sorted_servers = pGroup->sorted_servers;
	old_active_servers = pGroup->active_servers;
	old_last_sync_timestamps = pGroup->last_sync_timestamps;

	pGroup->alloc_size = new_size;
	pGroup->all_servers = new_servers;
	pGroup->sorted_servers = new_sorted_servers;
	pGroup->active_servers = new_active_servers;
	pGroup->last_sync_timestamps = new_last_sync_timestamps;

	tracker_mem_find_store_server(pGroup);

#ifdef WITH_HTTPD
	if (g_http_check_interval <= 0)
	{
		old_http_servers = NULL;
		pGroup->http_servers = pGroup->active_servers;
	}
	else
	{
		old_http_servers = pGroup->http_servers;
		pGroup->http_servers = new_http_servers;
		g_http_servers_dirty = true;
	}
#endif

	if (bNeedSleep)
	{
		sleep(1);
	}

	free(old_servers);
	
	free(old_sorted_servers);
	free(old_active_servers);

#ifdef WITH_HTTPD
	if (old_http_servers != NULL)
	{
		free(old_http_servers);
	}
#endif

	tracker_free_last_sync_timestamps(old_last_sync_timestamps, \
				old_size);

	return 0;
}

static int tracker_mem_cmp_by_group_name(const void *p1, const void *p2)
{
	return strcmp((*((FDFSGroupInfo **)p1))->group_name,
			(*((FDFSGroupInfo **)p2))->group_name);
}

static int tracker_mem_cmp_by_ip_addr(const void *p1, const void *p2)
{
	return strcmp((*((FDFSStorageDetail **)p1))->ip_addr,
			(*((FDFSStorageDetail **)p2))->ip_addr);
}

static void tracker_mem_insert_into_sorted_servers( \
		FDFSStorageDetail *pTargetServer,   \
		FDFSStorageDetail **sorted_servers, const int count)
{
	FDFSStorageDetail **ppServer;
	FDFSStorageDetail **ppEnd;

	ppEnd = sorted_servers + count;
	for (ppServer=ppEnd; ppServer>sorted_servers; ppServer--)
	{
		if (strcmp(pTargetServer->ip_addr, \
			   (*(ppServer-1))->ip_addr) > 0)
		{
			*ppServer = pTargetServer;
			return;
		}
		else
		{
			*ppServer = *(ppServer-1);
		}
	}

	*ppServer = pTargetServer;
}

static void tracker_mem_insert_into_sorted_groups(FDFSGroups *pGroups, \
		FDFSGroupInfo *pTargetGroup)
{
	FDFSGroupInfo **ppGroup;
	FDFSGroupInfo **ppEnd;

	ppEnd = pGroups->sorted_groups + pGroups->count;
	for (ppGroup=ppEnd; ppGroup > pGroups->sorted_groups; ppGroup--)
	{
		if (strcmp(pTargetGroup->group_name, \
			   (*(ppGroup-1))->group_name) > 0)
		{
			*ppGroup = pTargetGroup;
			return;
		}
		else
		{
			*ppGroup = *(ppGroup-1);
		}
	}

	*ppGroup = pTargetGroup;
}

FDFSGroupInfo *tracker_mem_get_group_ex(FDFSGroups *pGroups, \
		const char *group_name)
{
	FDFSGroupInfo target_groups;
	FDFSGroupInfo *pTargetGroups;
	FDFSGroupInfo **ppGroup;

	memset(&target_groups, 0, sizeof(target_groups));
	strcpy(target_groups.group_name, group_name);
	pTargetGroups = &target_groups;
	ppGroup = (FDFSGroupInfo **)bsearch(&pTargetGroups, \
			pGroups->sorted_groups, \
			pGroups->count, sizeof(FDFSGroupInfo *), \
			tracker_mem_cmp_by_group_name);

	if (ppGroup != NULL)
	{
		return *ppGroup;
	}
	else
	{
		return NULL;
	}
}

static int tracker_mem_add_group_ex(FDFSGroups *pGroups, \
	TrackerClientInfo *pClientInfo, const char *group_name, \
	const bool bNeedSleep, bool *bInserted)
{
	FDFSGroupInfo *pGroup;
	int result;

	if ((result=pthread_mutex_lock(&mem_thread_lock)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call pthread_mutex_lock fail, " \
			"errno: %d, error info: %s", \
			__LINE__, result, STRERROR(result));
		return result;
	}

	do
	{	
		result = 0;
		*bInserted = false;
		pGroup = tracker_mem_get_group_ex(pGroups, group_name);
		if (pGroup != NULL)
		{
			break;
		}

		if (pGroups->count >= pGroups->alloc_size)
		{
			result = tracker_mem_realloc_groups(pGroups, bNeedSleep);
			if (result != 0)
			{
				break;
			}
		}

		pGroup = *(pGroups->groups + pGroups->count);
		result = tracker_mem_init_group(pGroup);
		if (result != 0)
		{
			break;
		}

		strcpy(pGroup->group_name, group_name);
		tracker_mem_insert_into_sorted_groups(pGroups, pGroup);
		pGroups->count++;

		if ((pGroups->store_lookup == \
				FDFS_STORE_LOOKUP_SPEC_GROUP) && \
				(pGroups->pStoreGroup == NULL) && \
				(strcmp(pGroups->store_group, \
					pGroup->group_name) == 0))
		{
			pGroups->pStoreGroup = pGroup;
		}

		*bInserted = true;
	} while (0);

	if (pthread_mutex_unlock(&mem_thread_lock) != 0)
	{
		logError("file: "__FILE__", line: %d, "   \
			"call pthread_mutex_unlock fail", \
			__LINE__);
	}

	if (result != 0)
	{
		return result;
	}

	pClientInfo->pGroup = pGroup;
	return 0;
}

FDFSStorageDetail *tracker_mem_get_active_storage(FDFSGroupInfo *pGroup, \
				const char *ip_addr)
{
	FDFSStorageDetail target_storage;
	FDFSStorageDetail *pTargetStorage;
	FDFSStorageDetail **ppStorageServer;

	if (ip_addr == NULL)
	{
		return NULL;
	}

	memset(&target_storage, 0, sizeof(target_storage));
	strcpy(target_storage.ip_addr, ip_addr);
	pTargetStorage = &target_storage;
	ppStorageServer = (FDFSStorageDetail **)bsearch(&pTargetStorage, \
			pGroup->active_servers, \
			pGroup->active_count, \
			sizeof(FDFSStorageDetail *), \
			tracker_mem_cmp_by_ip_addr);
	if (ppStorageServer != NULL)
	{
		return *ppStorageServer;
	}
	else
	{
		return NULL;
	}
}

#ifdef WITH_HTTPD
static FDFSStorageDetail *tracker_mem_get_active_http_server( \
			FDFSGroupInfo *pGroup, const char *ip_addr)
{
	FDFSStorageDetail target_storage;
	FDFSStorageDetail *pTargetStorage;
	FDFSStorageDetail **ppStorageServer;

	memset(&target_storage, 0, sizeof(target_storage));
	strcpy(target_storage.ip_addr, ip_addr);
	pTargetStorage = &target_storage;
	ppStorageServer = (FDFSStorageDetail **)bsearch(&pTargetStorage, \
			pGroup->http_servers, \
			pGroup->http_server_count, \
			sizeof(FDFSStorageDetail *), \
			tracker_mem_cmp_by_ip_addr);
	if (ppStorageServer != NULL)
	{
		return *ppStorageServer;
	}
	else
	{
		return NULL;
	}
}
#endif

FDFSStorageDetail *tracker_mem_get_storage(FDFSGroupInfo *pGroup, \
				const char *ip_addr)
{
	FDFSStorageDetail target_storage;
	FDFSStorageDetail *pTargetStorage;
	FDFSStorageDetail **ppStorageServer;

	memset(&target_storage, 0, sizeof(target_storage));
	strcpy(target_storage.ip_addr, ip_addr);
	pTargetStorage = &target_storage;
	ppStorageServer = (FDFSStorageDetail **)bsearch(&pTargetStorage, \
			pGroup->sorted_servers, \
			pGroup->count, \
			sizeof(FDFSStorageDetail *), \
			tracker_mem_cmp_by_ip_addr);
	if (ppStorageServer != NULL)
	{
		return *ppStorageServer;
	}
	else
	{
		return NULL;
	}
}

static void tracker_mem_clear_storage_fields(FDFSStorageDetail *pStorageServer)
{
        if (pStorageServer->path_total_mbs != NULL)
	{
		memset(pStorageServer->path_total_mbs, 0, sizeof(int64_t) \
			* pStorageServer->store_path_count);
	}

        if (pStorageServer->path_free_mbs != NULL)
	{
		memset(pStorageServer->path_free_mbs, 0, sizeof(int64_t) \
			* pStorageServer->store_path_count);
	}

	pStorageServer->psync_src_server = NULL;
	pStorageServer->sync_until_timestamp = 0;
	pStorageServer->total_mb = 0;
	pStorageServer->free_mb = 0;
	pStorageServer->changelog_offset = 0;
	pStorageServer->store_path_count = 0;
	pStorageServer->subdir_count_per_path = 0;
	pStorageServer->upload_priority = 0;
	pStorageServer->current_write_path = 0;

	memset(&(pStorageServer->stat), 0, sizeof(FDFSStorageStat));
}

int tracker_mem_delete_storage(FDFSGroupInfo *pGroup, const char *ip_addr)
{
	FDFSStorageDetail *pStorageServer;
	FDFSStorageDetail **ppServer;
	FDFSStorageDetail **ppEnd;

	pStorageServer = tracker_mem_get_storage(pGroup, ip_addr);
	if (pStorageServer == NULL || pStorageServer->status == \
		FDFS_STORAGE_STATUS_IP_CHANGED)
	{
		return ENOENT;
	}

	if (pStorageServer->status == FDFS_STORAGE_STATUS_ONLINE || \
	    pStorageServer->status == FDFS_STORAGE_STATUS_ACTIVE || \
	    pStorageServer->status == FDFS_STORAGE_STATUS_RECOVERY)
	{
		return EBUSY;
	}

	if (pStorageServer->status == FDFS_STORAGE_STATUS_DELETED)
	{
		return EALREADY;
	}

	ppEnd = pGroup->all_servers + pGroup->count;
	for (ppServer=pGroup->all_servers; ppServer<ppEnd; ppServer++)
	{
		if ((*ppServer)->psync_src_server != NULL && \
		strcmp((*ppServer)->psync_src_server->ip_addr, ip_addr) == 0)
		{
			(*ppServer)->psync_src_server = NULL;
		}
	}

	tracker_mem_clear_storage_fields(pStorageServer);

	pStorageServer->status = FDFS_STORAGE_STATUS_DELETED;
	pGroup->chg_count++;

	tracker_write_to_changelog(pGroup, pStorageServer, NULL);
	return 0;
}

int tracker_mem_storage_ip_changed(FDFSGroupInfo *pGroup, \
		const char *old_storage_ip, const char *new_storage_ip)
{
	FDFSStorageDetail *pOldStorageServer;
	FDFSStorageDetail *pNewStorageServer;
	int result;
	bool bInserted;

	pOldStorageServer = tracker_mem_get_storage(pGroup, old_storage_ip);
	if (pOldStorageServer == NULL || pOldStorageServer->status == \
		FDFS_STORAGE_STATUS_DELETED)
	{
		logError("file: "__FILE__", line: %d, " \
			"client ip: %s, old storage server: %s not exists", \
			__LINE__, new_storage_ip, old_storage_ip);
		return ENOENT;
	}

	if (pOldStorageServer->status == FDFS_STORAGE_STATUS_ONLINE || \
	    pOldStorageServer->status == FDFS_STORAGE_STATUS_ACTIVE || \
	    pOldStorageServer->status == FDFS_STORAGE_STATUS_RECOVERY)
	{
		logError("file: "__FILE__", line: %d, " \
			"client ip: %s, old storage server: %s is online", \
			__LINE__, new_storage_ip, old_storage_ip);
		return EBUSY;
	}

	if (pOldStorageServer->status == FDFS_STORAGE_STATUS_IP_CHANGED)
	{
		logError("file: "__FILE__", line: %d, " \
			"client ip: %s, old storage server: %s " \
			"'s ip address already changed", \
			__LINE__, new_storage_ip, old_storage_ip);
		return EALREADY;
	}

	pNewStorageServer = tracker_mem_get_storage(pGroup, new_storage_ip);
	if (!(pNewStorageServer == NULL || pNewStorageServer->status == \
		FDFS_STORAGE_STATUS_DELETED))
	{
		logError("file: "__FILE__", line: %d, " \
			"client ip: %s, new storage server: %s already exists",\
			__LINE__, new_storage_ip, new_storage_ip);
		return EEXIST;
	}

	result = _tracker_mem_add_storage(pGroup, \
		&pNewStorageServer, new_storage_ip, true, &bInserted);
	if (result != 0)
	{
		return result;
	}

	if (!bInserted)
	{
		logError("file: "__FILE__", line: %d, " \
			"client ip: %s, new storage server: %s already exists",\
			__LINE__, new_storage_ip, new_storage_ip);
		return EEXIST;
	}

	pthread_mutex_lock(&mem_thread_lock);

	//exchange old and new storage server
	snprintf(pOldStorageServer->ip_addr, \
		sizeof(pOldStorageServer->ip_addr), "%s", new_storage_ip);

	snprintf(pNewStorageServer->ip_addr, \
		sizeof(pNewStorageServer->ip_addr), "%s", old_storage_ip);
	pNewStorageServer->status = FDFS_STORAGE_STATUS_IP_CHANGED;

	pGroup->chg_count++;

	//need re-sort
	qsort(pGroup->sorted_servers, pGroup->count, \
		sizeof(FDFSStorageDetail *), tracker_mem_cmp_by_ip_addr);

	pthread_mutex_unlock(&mem_thread_lock);

	tracker_write_to_changelog(pGroup, pNewStorageServer, new_storage_ip);

	return tracker_save_sys_files();
}

static int tracker_mem_add_storage(TrackerClientInfo *pClientInfo, \
		const char *ip_addr, const bool bNeedSleep, bool *bInserted)
{
	int result;
	FDFSStorageDetail *pStorageServer;

	pStorageServer = NULL;
	result = _tracker_mem_add_storage(pClientInfo->pGroup, \
			&pStorageServer, ip_addr, bNeedSleep, bInserted);
	if (result == 0)
	{
		pClientInfo->pStorage = pStorageServer;
	}

	return result;
}

static int _tracker_mem_add_storage(FDFSGroupInfo *pGroup, \
	FDFSStorageDetail **ppStorageServer, const char *ip_addr, \
	const bool bNeedSleep, bool *bInserted)
{
	int result;

	if ((result=pthread_mutex_lock(&mem_thread_lock)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call pthread_mutex_lock fail, " \
			"errno: %d, error info: %s", \
			__LINE__, result, STRERROR(result));
		return result;
	}

	do
	{
		result = 0;
		*bInserted = false;
		*ppStorageServer = tracker_mem_get_storage(pGroup, ip_addr);
		if (*ppStorageServer != NULL)
		{
			if ((*ppStorageServer)->status==FDFS_STORAGE_STATUS_DELETED \
			 || (*ppStorageServer)->status==FDFS_STORAGE_STATUS_IP_CHANGED)
			{
			 	(*ppStorageServer)->status = FDFS_STORAGE_STATUS_INIT;
			}

			break;
		}

		if (pGroup->count >= pGroup->alloc_size)
		{
			result = tracker_mem_realloc_store_servers( \
					pGroup, 1, bNeedSleep);
			if (result != 0)
			{
				break;
			}
		}

		*ppStorageServer = *(pGroup->all_servers + pGroup->count);
		memcpy((*ppStorageServer)->ip_addr, ip_addr, IP_ADDRESS_SIZE);

		tracker_mem_insert_into_sorted_servers(*ppStorageServer, \
				pGroup->sorted_servers, pGroup->count);
		pGroup->count++;
		pGroup->chg_count++;

		*bInserted = true;
	} while (0);

	if (pthread_mutex_unlock(&mem_thread_lock) != 0)
	{
		logError("file: "__FILE__", line: %d, "   \
			"call pthread_mutex_unlock fail", \
			__LINE__);
	}

	return result;
}

static int tracker_mem_get_status(TrackerServerInfo *pTrackerServer, \
		TrackerRunningStatus *pStatus)
{
	char in_buff[2 * FDFS_PROTO_PKG_LEN_SIZE];
	TrackerHeader header;
	char *pInBuff;
	int64_t in_bytes;
	int result;

	pTrackerServer->sock = -1;
	if ((result=tracker_connect_server(pTrackerServer)) != 0)
	{
		return result;
	}

	do
	{
	memset(&header, 0, sizeof(header));
	header.cmd = TRACKER_PROTO_CMD_TRACKER_GET_STATUS;
	if ((result=tcpsenddata_nb(pTrackerServer->sock, &header, \
			sizeof(header), g_fdfs_network_timeout)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"send data to tracker server %s:%d fail, " \
			"errno: %d, error info: %s", __LINE__, \
			pTrackerServer->ip_addr, \
			pTrackerServer->port, \
			result, STRERROR(result));

		result = (result == ENOENT ? EACCES : result);
		break;
	}

	pInBuff = in_buff;
	result = fdfs_recv_response(pTrackerServer, &pInBuff, \
				sizeof(in_buff), &in_bytes);
	if (result != 0)
	{
		break;
	}

	if (in_bytes != sizeof(in_buff))
	{
		logError("file: "__FILE__", line: %d, " \
			"tracker server %s:%d response data " \
			"length: "INT64_PRINTF_FORMAT" is invalid, " \
			"expect length: %d.", __LINE__, \
			pTrackerServer->ip_addr, pTrackerServer->port, \
			in_bytes, (int)sizeof(in_buff));
		result = EINVAL;
		break;
	}

	pStatus->running_time = buff2long(in_buff);
	pStatus->restart_interval = buff2long(in_buff + FDFS_PROTO_PKG_LEN_SIZE);

	} while (0);

	close(pTrackerServer->sock);
	pTrackerServer->sock = -1;

	return result;
}

void tracker_calc_running_times(TrackerRunningStatus *pStatus)
{
	pStatus->running_time = time(NULL) - g_up_time;

	if (g_tracker_last_status.last_check_time == 0)
	{
		pStatus->restart_interval = 0;
	}
	else
	{
		pStatus->restart_interval = g_up_time - \
				g_tracker_last_status.last_check_time;
	}

#define FDFS_TRIM_TIME(t, i) (t / i) * i

	pStatus->running_time = FDFS_TRIM_TIME(pStatus->running_time, \
					TRACKER_SYNC_STATUS_FILE_INTERVAL);
	pStatus->restart_interval = FDFS_TRIM_TIME(pStatus->restart_interval, \
					TRACKER_SYNC_STATUS_FILE_INTERVAL);
}

static int tracker_mem_get_sys_file_piece(TrackerServerInfo *pTrackerServer, \
	const int file_index, int fd, int64_t *offset, int64_t *file_size)
{
	char out_buff[sizeof(TrackerHeader) + 1 + FDFS_PROTO_PKG_LEN_SIZE];
	char in_buff[TRACKER_MAX_PACKAGE_SIZE];
	TrackerHeader *pHeader;
	char *p;
	char *pInBuff;
	char *pContent;
	int64_t in_bytes;
	int64_t write_bytes;
	int result;

	memset(out_buff, 0, sizeof(out_buff));
	pHeader = (TrackerHeader *)out_buff;
	pHeader->cmd = TRACKER_PROTO_CMD_TRACKER_GET_ONE_SYS_FILE;
	long2buff(1 + FDFS_PROTO_PKG_LEN_SIZE, pHeader->pkg_len);

	p = out_buff + sizeof(TrackerHeader);
	*p++ = file_index;
	long2buff(*offset, p);
	if ((result=tcpsenddata_nb(pTrackerServer->sock, out_buff, \
			sizeof(out_buff), g_fdfs_network_timeout)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"send data to tracker server %s:%d fail, " \
			"errno: %d, error info: %s", __LINE__, \
			pTrackerServer->ip_addr, \
			pTrackerServer->port, \
			result, STRERROR(result));

		return (result == ENOENT ? EACCES : result);
	}

	pInBuff = in_buff;
	result = fdfs_recv_response(pTrackerServer, &pInBuff, \
				sizeof(in_buff), &in_bytes);
	if (result != 0)
	{
		return result;
	}

	if (in_bytes < FDFS_PROTO_PKG_LEN_SIZE)
	{
		logError("file: "__FILE__", line: %d, " \
			"tracker server %s:%d response data " \
			"length: "INT64_PRINTF_FORMAT" is invalid, " \
			"expect length >= %d.", __LINE__, \
			pTrackerServer->ip_addr, pTrackerServer->port, \
			in_bytes, FDFS_PROTO_PKG_LEN_SIZE);
		return EINVAL;
	}

	*file_size = buff2long(in_buff);
	write_bytes = in_bytes - FDFS_PROTO_PKG_LEN_SIZE;

	if (*file_size < 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"tracker server %s:%d, file size: "INT64_PRINTF_FORMAT\
			" < 0", __LINE__, pTrackerServer->ip_addr, \
			pTrackerServer->port, *file_size);
		return EINVAL;
	}

	if (*file_size > 0 && write_bytes == 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"tracker server %s:%d, file size: "INT64_PRINTF_FORMAT\
			" > 0, but file content is empty", __LINE__, \
			pTrackerServer->ip_addr, pTrackerServer->port, \
			*file_size);
		return EINVAL;
	}

	pContent = pInBuff + FDFS_PROTO_PKG_LEN_SIZE;
	if (write_bytes > 0 && write(fd, pContent, write_bytes) != write_bytes)
	{
		logError("file: "__FILE__", line: %d, " \
			"write to file %s fail, " \
			"errno: %d, error info: %s", \
			__LINE__, g_tracker_sys_filenames[file_index], \
			errno, STRERROR(errno));
		return errno != 0 ? errno : EIO;
        }

	*offset += write_bytes;
	return 0;
}

static int tracker_mem_get_one_sys_file(TrackerServerInfo *pTrackerServer, \
		const int file_index)
{
	char full_filename[MAX_PATH_SIZE];
	int fd;
	int result;
	int64_t offset;
	int64_t file_size;

	snprintf(full_filename, sizeof(full_filename), "%s/data/%s", \
			g_fdfs_base_path, g_tracker_sys_filenames[file_index]);
	fd = open(full_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"open file %s fail, " \
			"errno: %d, error info: %s", \
			__LINE__, full_filename, \
			errno, STRERROR(errno));
		return errno != 0 ? errno : EACCES;
	}

	TRACKER_FCHOWN(fd, full_filename, geteuid(), getegid())

	offset = 0;
	file_size = 0;
	while (1)
	{
		result = tracker_mem_get_sys_file_piece(pTrackerServer, \
			file_index, fd, &offset, &file_size);
		if (result != 0)
		{
			break;
		}

		if (offset >= file_size)
		{
			break;
		}
	}

	close(fd);
	return result;
}

static int tracker_mem_get_sys_files(TrackerServerInfo *pTrackerServer)
{
	int result;
	int index;

	pTrackerServer->sock = -1;
	if ((result=tracker_connect_server(pTrackerServer)) != 0)
	{
		return result;
	}

	if ((result=tracker_get_sys_files_start(pTrackerServer)) != 0)
	{
		close(pTrackerServer->sock);
		return result;
	}

	for (index=0; index<TRACKER_SYS_FILE_COUNT; index++)
	{
		result = tracker_mem_get_one_sys_file(pTrackerServer, index);
		if (result != 0)
		{
			break;
		}
	}

	result = tracker_get_sys_files_end(pTrackerServer);

	close(pTrackerServer->sock);
	return result;
}

static int tracker_mem_cmp_tracker_running_status(const void *p1, const void *p2)
{
	TrackerRunningStatus *pStatus1;
	TrackerRunningStatus *pStatus2;
	int sub;

	pStatus1 = (TrackerRunningStatus *)p1;
	pStatus2 = (TrackerRunningStatus *)p2;

	sub = pStatus1->running_time - pStatus2->running_time;
	if (sub != 0)
	{
		return sub;
	}

	return pStatus2->restart_interval - pStatus1->restart_interval;
}

static int tracker_mem_save_tracker_servers(FDFSStorageJoinBody *pJoinBody)
{
	int bytes;

	tracker_group.server_count = pJoinBody->tracker_count;
	bytes = sizeof(TrackerServerInfo) * tracker_group.server_count;
	tracker_group.servers == (TrackerServerInfo *)malloc(bytes);
	if (tracker_group.servers == NULL)
	{
		logError("file: "__FILE__", line: %d, " \
			"malloc %d bytes fail, " \
			"errno: %d, error info: %s", \
			__LINE__, bytes, errno, STRERROR(errno));
		return errno != 0 ? errno : ENOMEM;
	}

	memcpy(tracker_group.servers, pJoinBody->tracker_servers, bytes);
	return 0;
}

static int tracker_mem_check_tracker_servers(FDFSStorageJoinBody *pJoinBody)
{
	TrackerServerInfo *pJoinTracker;
	TrackerServerInfo *pJoinEnd;
	TrackerServerInfo *pLocalTracker;
	TrackerServerInfo *pLocalEnd;
	int new_count;
	int bytes;

	new_count = 0;
	pLocalEnd = tracker_group.servers + tracker_group.server_count;
	pJoinEnd = pJoinBody->tracker_servers + pJoinBody->tracker_count;
        for (pJoinTracker=pJoinBody->tracker_servers; \
                pJoinTracker<pJoinEnd; pJoinTracker++)
	{
        	for (pLocalTracker=tracker_group.servers; \
               		pLocalTracker<pLocalEnd; pLocalTracker++)
		{
			if (pJoinTracker->port == pLocalTracker->port && \
				strcmp(pJoinTracker->ip_addr, \
					pLocalTracker->ip_addr) == 0)
			{
				break;
			}
		}

		if (pLocalTracker == pLocalEnd)
		{
			new_count++;
		}
	}

	bytes = sizeof(TrackerServerInfo) * tracker_group.server_count;
	tracker_group.servers == (TrackerServerInfo *)malloc(bytes);
	if (tracker_group.servers == NULL)
	{
		logError("file: "__FILE__", line: %d, " \
			"malloc %d bytes fail, " \
			"errno: %d, error info: %s", \
			__LINE__, bytes, errno, STRERROR(errno));
		return errno != 0 ? errno : ENOMEM;
	}

	memcpy(tracker_group.servers, pJoinBody->tracker_servers, bytes);
	return 0;
}

static int tracker_mem_get_tracker_server(FDFSStorageJoinBody *pJoinBody, \
		TrackerRunningStatus *pTrackerStatus)
{
	TrackerServerInfo *pTrackerServer;
	TrackerServerInfo *pTrackerEnd;
	TrackerRunningStatus trackerStatus[FDFS_MAX_TRACKERS];
	TrackerRunningStatus *pStatus;
	int count;
	int result;
	int r;
	int i;

	memset(pTrackerStatus, 0, sizeof(TrackerRunningStatus));
	pStatus = trackerStatus;
	result = 0;
	pTrackerEnd = pJoinBody->tracker_servers + pJoinBody->tracker_count;
        for (pTrackerServer=pJoinBody->tracker_servers; \
                pTrackerServer<pTrackerEnd; pTrackerServer++)
	{
		if (pTrackerServer->port == g_server_port && \
			is_local_host_ip(pTrackerServer->ip_addr))
		{
			continue;
		}

		pStatus->pTrackerServer = pTrackerServer;
		r = tracker_mem_get_status(pTrackerServer, pStatus);
		if (r == 0)
		{
			pStatus++;
		}
		else if (r != ENOENT)
		{
			result = r;
		}
	}

	count = pStatus - trackerStatus;
	if (count == 0)
	{
		return result == 0 ? ENOENT : result;
	}

	if (count == 1)
	{
		memcpy(pTrackerStatus, trackerStatus, \
			sizeof(TrackerRunningStatus));
		return 0;
	}

	qsort(trackerStatus, count, sizeof(TrackerRunningStatus), \
		tracker_mem_cmp_tracker_running_status);

	for (i=0; i<count; i++)
	{
		logDebug("file: "__FILE__", line: %d, " \
			"%s:%d running time: %d, restart interval: %d", \
			__LINE__, \
			trackerStatus[i].pTrackerServer->ip_addr, \
			trackerStatus[i].pTrackerServer->port, \
			trackerStatus[i].running_time, \
			trackerStatus[i].restart_interval);
	}

	//copy the last
	memcpy(pTrackerStatus, trackerStatus + (count - 1), \
			sizeof(TrackerRunningStatus));
	return 0;
}

static int tracker_mem_get_sys_files_from_others(FDFSStorageJoinBody *pJoinBody,
		 TrackerRunningStatus *pRunningStatus)
{
	int result;
	TrackerRunningStatus trackerStatus;
	TrackerServerInfo *pTrackerServer;
	FDFSGroups newGroups;
	FDFSGroups tempGroups;

	if (pJoinBody->tracker_count == 0)
	{
		return 0;
	}

	result = tracker_mem_get_tracker_server(pJoinBody, &trackerStatus);
	if (result != 0)
	{
		return result == ENOENT ? 0 : result;
	}

	if (pRunningStatus != NULL)
	{
		if (tracker_mem_cmp_tracker_running_status(pRunningStatus, \
							&trackerStatus) >= 0)
		{
			logDebug("file: "__FILE__", line: %d, " \
				"%s:%d running time: %d, restart interval: %d, "\
				"my running time: %d, restart interval: %d, " \
				"do not need sync system files", __LINE__, \
				trackerStatus.pTrackerServer->ip_addr, \
				trackerStatus.pTrackerServer->port, \
				trackerStatus.running_time, \
				trackerStatus.restart_interval, \
				pRunningStatus->running_time, \
				pRunningStatus->restart_interval);
			
			return 0;
		}
	}

	pTrackerServer = trackerStatus.pTrackerServer;
	result = tracker_mem_get_sys_files(pTrackerServer);
	if (result != 0)
	{
		return result;
	}

	logInfo("file: "__FILE__", line: %d, " \
		"sys files loaded from tracker server %s:%d", \
		__LINE__, pTrackerServer->ip_addr, \
		pTrackerServer->port);

	memset(&newGroups, 0, sizeof(newGroups));
	newGroups.store_lookup = g_groups.store_lookup;
	newGroups.store_server = g_groups.store_server;
	newGroups.download_server = g_groups.download_server;
	newGroups.store_path = g_groups.store_path;
	strcpy(newGroups.store_group, g_groups.store_group);
	if ((result=tracker_mem_init_groups(&newGroups)) != 0)
	{
 		tracker_mem_destroy_groups(&newGroups, false);
		return result;
	}

	memcpy(&tempGroups, &g_groups, sizeof(FDFSGroups));
	memcpy(&g_groups, &newGroups, sizeof(FDFSGroups));

	usleep(100000);

 	tracker_mem_destroy_groups(&tempGroups, false);
	tracker_write_status_to_file(NULL);

	if (changelog_fd >= 0)
	{
		close(changelog_fd);
		changelog_fd = -1;
	}

	return tracker_open_changlog_file();
}

int tracker_mem_add_group_and_storage(TrackerClientInfo *pClientInfo, \
		const char *ip_addr, FDFSStorageJoinBody *pJoinBody, \
		const bool bNeedSleep)
{
	int result;
	bool bStorageInserted;
	bool bGroupInserted;
	FDFSStorageDetail *pStorageServer;
	FDFSStorageDetail **ppServer;
	FDFSStorageDetail **ppEnd;

	if (tracker_group.servers == NULL)
	{
		if ((result=tracker_mem_save_tracker_servers(pJoinBody)) != 0)
		{
			return result;
		}
	}
	else
	{
	}

	if (need_get_sys_files)
	{
		tracker_mem_file_lock();

		if (need_get_sys_files)
		{
			if (g_tracker_last_status.last_check_time > 0 && \
			g_up_time - g_tracker_last_status.last_check_time > \
				2 * TRACKER_SYNC_STATUS_FILE_INTERVAL)
			{ /* stop time exceeds 2 * interval */
				TrackerRunningStatus runningStatus;

				tracker_calc_running_times(&runningStatus);
				result = tracker_mem_get_sys_files_from_others(\
						pJoinBody, &runningStatus);
				if (result != 0)
				{
					tracker_mem_file_unlock();
					return EAGAIN;
				}

				get_sys_files_done = true;
			}

			need_get_sys_files = false;
		}

		tracker_mem_file_unlock();
	}

	if ((!get_sys_files_done) && (g_groups.count == 0))
	{
		tracker_mem_file_lock();
		if (g_groups.count == 0)
		{
			if ((result=tracker_mem_get_sys_files_from_others( \
				pJoinBody, NULL)) != 0)
			{
				tracker_mem_file_unlock();
				return EAGAIN;
			}

			get_sys_files_done = true;
		}
		tracker_mem_file_unlock();
	}

	if ((result=tracker_mem_add_group_ex(&g_groups, pClientInfo, \
		pJoinBody->group_name, bNeedSleep, &bGroupInserted)) != 0)
	{
		return result;
	}

	if (bGroupInserted)
	{
		if ((result=tracker_save_groups()) != 0)
		{
			return result;
		}
	}

	if (pClientInfo->pGroup->storage_port == 0)
	{
		pClientInfo->pGroup->storage_port = pJoinBody->storage_port;
		if ((result=tracker_save_groups()) != 0)
		{
			return result;
		}
	}
	else
	{
		if (pClientInfo->pGroup->storage_port !=  \
			pJoinBody->storage_port)
		{
			ppEnd = pClientInfo->pGroup->all_servers + \
				pClientInfo->pGroup->count;
			for (ppServer=pClientInfo->pGroup->all_servers; \
				ppServer<ppEnd; ppServer++)
			{
				if (strcmp((*ppServer)->ip_addr, ip_addr) == 0)
				{
					(*ppServer)->storage_port = \
						pJoinBody->storage_port;
					break;
				}
			}

			for (ppServer=pClientInfo->pGroup->all_servers; \
				ppServer<ppEnd; ppServer++)
			{
				if ((*ppServer)->storage_port != \
					pJoinBody->storage_port)
				{
					break;
				}
			}
			if (ppServer == ppEnd)  //all servers are same, adjust
			{
				pClientInfo->pGroup->storage_port = \
						pJoinBody->storage_port;
				if ((result=tracker_save_groups()) != 0)
				{
					return result;
				}
			}
			else
			{
			logError("file: "__FILE__", line: %d, " \
				"client ip: %s, port %d is not same " \
				"in the group \"%s\", group port is %d", \
				__LINE__, ip_addr, pJoinBody->storage_port, \
				pJoinBody->group_name, \
				pClientInfo->pGroup->storage_port);
			return EINVAL;
			}
		}
	}

	if (pClientInfo->pGroup->storage_http_port == 0)
	{
		pClientInfo->pGroup->storage_http_port = \
			pJoinBody->storage_http_port;
		if ((result=tracker_save_groups()) != 0)
		{
			return result;
		}
	}
	else
	{
		if (pClientInfo->pGroup->storage_http_port !=  \
			pJoinBody->storage_http_port)
		{
			ppEnd = pClientInfo->pGroup->all_servers + \
				pClientInfo->pGroup->count;
			for (ppServer=pClientInfo->pGroup->all_servers; \
				ppServer<ppEnd; ppServer++)
			{
				if (strcmp((*ppServer)->ip_addr, ip_addr) == 0)
				{
					(*ppServer)->storage_http_port = \
						pJoinBody->storage_http_port;
					break;
				}
			}

			for (ppServer=pClientInfo->pGroup->all_servers; \
				ppServer<ppEnd; ppServer++)
			{
				if ((*ppServer)->storage_http_port != \
					pJoinBody->storage_http_port)
				{
					break;
				}
			}
			if (ppServer == ppEnd)  //all servers are same, adjust
			{
				pClientInfo->pGroup->storage_http_port = \
					pJoinBody->storage_http_port;
				if ((result=tracker_save_groups()) != 0)
				{
					return result;
				}
			}
			else
			{
			logError("file: "__FILE__", line: %d, " \
				"client ip: %s, http port %d is not same " \
				"in the group \"%s\", group http port is %d", \
				__LINE__, ip_addr, \
				pJoinBody->storage_http_port, \
				pJoinBody->group_name, \
				pClientInfo->pGroup->storage_http_port);
#ifdef WITH_HTTPD
			return EINVAL;
#endif
			}
		}
	}
	
	if ((result=pthread_mutex_lock(&mem_thread_lock)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call pthread_mutex_lock fail, " \
			"errno: %d, error info: %s", \
			__LINE__, result, STRERROR(result));
		return result;
	}

	pStorageServer = tracker_mem_get_storage(pClientInfo->pGroup, ip_addr);

	if (pthread_mutex_unlock(&mem_thread_lock) != 0)
	{
		logError("file: "__FILE__", line: %d, "   \
			"call pthread_mutex_unlock fail", \
			__LINE__);
	}

	if (pStorageServer == NULL)
	{
		if (!pJoinBody->init_flag)
		{
			if (pJoinBody->status < 0 || \
			pJoinBody->status == FDFS_STORAGE_STATUS_DELETED || \
			pJoinBody->status == FDFS_STORAGE_STATUS_IP_CHANGED || \
			pJoinBody->status == FDFS_STORAGE_STATUS_NONE)
			{
				logError("file: "__FILE__", line: %d, " \
					"client ip: %s:%d, invalid storage " \
					"status %d, in the group \"%s\"", \
					__LINE__, ip_addr, \
					pJoinBody->storage_port, \
					pJoinBody->status, \
					pJoinBody->group_name);
				return EFAULT;
			}
		}
	}

	if ((result=tracker_mem_add_storage(pClientInfo, ip_addr, \
			bNeedSleep, &bStorageInserted)) != 0)
	{
		return result;
	}

	pStorageServer = pClientInfo->pStorage;
	pStorageServer->store_path_count = pJoinBody->store_path_count;
	pStorageServer->subdir_count_per_path = pJoinBody->subdir_count_per_path;
	pStorageServer->upload_priority = pJoinBody->upload_priority;
	pStorageServer->join_time = pJoinBody->join_time;
	pStorageServer->up_time = pJoinBody->up_time;
	snprintf(pStorageServer->version, sizeof(pStorageServer->version), \
		"%s", pJoinBody->version);
	snprintf(pStorageServer->domain_name, \
		sizeof(pStorageServer->domain_name), \
		"%s", pJoinBody->domain_name);
	pStorageServer->storage_port = pJoinBody->storage_port;
	pStorageServer->storage_http_port = pJoinBody->storage_http_port;

	if (pClientInfo->pGroup->store_path_count == 0)
	{
		pClientInfo->pGroup->store_path_count = \
				pJoinBody->store_path_count;
		if ((result=tracker_malloc_group_path_mbs( \
				pClientInfo->pGroup)) != 0)
		{
			return result;
		}
		if ((result=tracker_save_groups()) != 0)
		{
			return result;
		}
	}
	else
	{
		if (pClientInfo->pGroup->store_path_count != \
			pJoinBody->store_path_count)
		{
			ppEnd = pClientInfo->pGroup->all_servers + \
				pClientInfo->pGroup->count;
			for (ppServer=pClientInfo->pGroup->all_servers; \
				ppServer<ppEnd; ppServer++)
			{
				if ((*ppServer)->store_path_count != \
					pJoinBody->store_path_count)
				{
					break;
				}
			}

			if (ppServer == ppEnd)  //all servers are same, adjust
			{
				if ((result=tracker_realloc_group_path_mbs( \
			 	    pClientInfo->pGroup, \
				    pJoinBody->store_path_count))!=0)
				{
					return result;
				}

				if ((result=tracker_save_groups()) != 0)
				{
					return result;
				}

				logDebug("file: "__FILE__", line: %d, " \
				"all storage server's store_path_count " \
				"are same, adjust to %d", \
				__LINE__, pJoinBody->store_path_count);
			}
			else
			{
				logError("file: "__FILE__", line: %d, " \
				"client ip: %s, store_path_count %d is not " \
				"same in the group \"%s\", " \
				"group store_path_count is %d", \
				__LINE__, ip_addr, \
				pJoinBody->store_path_count, \
				pJoinBody->group_name, \
				pClientInfo->pGroup->store_path_count);

				return EINVAL;
			}
		}
	}

	if (pClientInfo->pGroup->subdir_count_per_path == 0)
	{
		pClientInfo->pGroup->subdir_count_per_path = \
				pJoinBody->subdir_count_per_path;
		if ((result=tracker_save_groups()) != 0)
		{
			return result;
		}
	}
	else
	{
		if (pClientInfo->pGroup->subdir_count_per_path != \
				pJoinBody->subdir_count_per_path)
		{
			ppEnd = pClientInfo->pGroup->all_servers + \
				pClientInfo->pGroup->count;
			for (ppServer=pClientInfo->pGroup->all_servers; \
				ppServer<ppEnd; ppServer++)
			{
				if ((*ppServer)->subdir_count_per_path != \
					pJoinBody->subdir_count_per_path)
				{
					break;
				}
			}

			if (ppServer == ppEnd)  //all servers are same, adjust
			{
				pClientInfo->pGroup->subdir_count_per_path = \
					pJoinBody->subdir_count_per_path;
				if ((result=tracker_save_groups()) != 0)
				{
					return result;
				}
			}
			else
			{
				logError("file: "__FILE__", line: %d, " \
				"client ip: %s, subdir_count_per_path %d is " \
				"not same in the group \"%s\", " \
				"group subdir_count_per_path is %d", \
				__LINE__, ip_addr, \
				pJoinBody->subdir_count_per_path, \
				pJoinBody->group_name,\
				pClientInfo->pGroup->subdir_count_per_path);

				return EINVAL;
			}
		}
	}

	if (bStorageInserted)
	{
		if ((!pJoinBody->init_flag) && pJoinBody->status > 0)
		{
			if (pJoinBody->status == FDFS_STORAGE_STATUS_ACTIVE)
			{
				pStorageServer->status = FDFS_STORAGE_STATUS_ONLINE;
			}
			else
			{
				pStorageServer->status = pJoinBody->status;
			}
		}

		if ((result=tracker_save_sys_files()) != 0)
		{
			return result;
		}
	}

	if (pStorageServer->status == FDFS_STORAGE_STATUS_OFFLINE || \
	    pStorageServer->status == FDFS_STORAGE_STATUS_RECOVERY)
	{
		pStorageServer->status = FDFS_STORAGE_STATUS_ONLINE;
	}
	else if (pStorageServer->status == FDFS_STORAGE_STATUS_INIT)
	{
	 	pStorageServer->changelog_offset = g_changelog_fsize;
	}

	logDebug("file: "__FILE__", line: %d, " \
		"storage server %s::%s join in, remain changelog bytes: " \
		INT64_PRINTF_FORMAT, __LINE__, \
		pClientInfo->pGroup->group_name, ip_addr, \
	 	g_changelog_fsize - pStorageServer->changelog_offset);

	return 0;
}

int tracker_mem_sync_storages(FDFSGroupInfo *pGroup, \
		FDFSStorageBrief *briefServers, const int server_count)
{
	int result;
	FDFSStorageBrief *pServer;
	FDFSStorageBrief *pEnd;
	FDFSStorageDetail target_storage;
	FDFSStorageDetail *pTargetStorage;
	FDFSStorageDetail *pStorageServer;
	FDFSStorageDetail **ppFound;

	if ((result=pthread_mutex_lock(&mem_thread_lock)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call pthread_mutex_lock fail, " \
			"errno: %d, error info: %s", \
			__LINE__, result, STRERROR(result));
		return result;
	}

	result = 0;
	do
	{
		if (pGroup->count + server_count >= pGroup->alloc_size)
		{
			result = tracker_mem_realloc_store_servers( \
					pGroup, server_count, true);
			if (result != 0)
			{
				break;
			}
		}

		memset(&target_storage, 0, sizeof(target_storage));
		pStorageServer = *(pGroup->all_servers + pGroup->count);
		pEnd = briefServers + server_count;
		for (pServer=briefServers; pServer<pEnd; pServer++)
		{
			pServer->ip_addr[IP_ADDRESS_SIZE-1] = '\0';
			if (pServer->status == FDFS_STORAGE_STATUS_NONE \
			 || pServer->status == FDFS_STORAGE_STATUS_ACTIVE \
			 || pServer->status == FDFS_STORAGE_STATUS_ONLINE)
			{
				continue;
			}

			memcpy(target_storage.ip_addr, pServer->ip_addr, \
				IP_ADDRESS_SIZE);
			pTargetStorage = &target_storage;
			if ((ppFound=(FDFSStorageDetail **)bsearch( \
				&pTargetStorage, \
				pGroup->sorted_servers, \
				pGroup->count, \
				sizeof(FDFSStorageDetail *), \
				tracker_mem_cmp_by_ip_addr)) != NULL)
			{
				if ((*ppFound)->status == pServer->status \
				 || (*ppFound)->status == \
					FDFS_STORAGE_STATUS_ONLINE \
				 || (*ppFound)->status == \
					FDFS_STORAGE_STATUS_ACTIVE
				 || (*ppFound)->status == \
					FDFS_STORAGE_STATUS_RECOVERY)
				{
					continue;
				}

				if (pServer->status == \
					FDFS_STORAGE_STATUS_DELETED
				 || pServer->status == \
					FDFS_STORAGE_STATUS_IP_CHANGED)
				{
					(*ppFound)->status = pServer->status;
					pGroup->chg_count++;
					continue;
				}

				if (pServer->status > (*ppFound)->status)
				{
					(*ppFound)->status = pServer->status;
					pGroup->chg_count++;
				}
			}
			else if (pServer->status == FDFS_STORAGE_STATUS_DELETED
			   || pServer->status == FDFS_STORAGE_STATUS_IP_CHANGED)
			{
				//ignore deleted storage server
			}
			else
			{
				pStorageServer->status = pServer->status;
				memcpy(pStorageServer->ip_addr, \
					pServer->ip_addr, IP_ADDRESS_SIZE);

				tracker_mem_insert_into_sorted_servers( \
					pStorageServer, pGroup->sorted_servers,\
					pGroup->count);

				pStorageServer++;
				pGroup->count++;
			}
		}
	} while (0);

	if (pthread_mutex_unlock(&mem_thread_lock) != 0)
	{
		logError("file: "__FILE__", line: %d, "   \
			"call pthread_mutex_unlock fail", \
			__LINE__);
	}

	return result;
}

static void tracker_mem_find_store_server(FDFSGroupInfo *pGroup)
{
	if (pGroup->active_count == 0)
	{
		pGroup->pStoreServer = NULL;
		return;
	}

	if (g_groups.store_server == FDFS_STORE_SERVER_FIRST_BY_PRI)
	{
		FDFSStorageDetail **ppEnd;
		FDFSStorageDetail **ppServer;
		FDFSStorageDetail *pMinPriServer;

		pMinPriServer = *(pGroup->active_servers);
		ppEnd = pGroup->active_servers + pGroup->active_count;
		for (ppServer=pGroup->active_servers+1; ppServer<ppEnd; \
			ppServer++)
		{
			if ((*ppServer)->upload_priority < \
				pMinPriServer->upload_priority)
			{
				pMinPriServer = *ppServer;
			}
		}

		pGroup->pStoreServer = pMinPriServer;
	}
	else
	{
		pGroup->pStoreServer = *(pGroup->active_servers);
	}
}

int tracker_mem_deactive_store_server(FDFSGroupInfo *pGroup,
			FDFSStorageDetail *pTargetServer) 
{
	int result;
	FDFSStorageDetail **ppStorageServer;
	FDFSStorageDetail **ppEnd;
	FDFSStorageDetail **ppServer;

	if ((result=pthread_mutex_lock(&mem_thread_lock)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call pthread_mutex_lock fail, " \
			"errno: %d, error info: %s", \
			__LINE__, result, STRERROR(result));
		return result;
	}

	ppStorageServer = (FDFSStorageDetail **)bsearch( \
			&pTargetServer, \
			pGroup->active_servers, \
			pGroup->active_count,   \
			sizeof(FDFSStorageDetail *), \
			tracker_mem_cmp_by_ip_addr);
	if (ppStorageServer != NULL)
	{
		ppEnd = pGroup->active_servers + pGroup->active_count - 1;
		for (ppServer=ppStorageServer; ppServer<ppEnd; ppServer++)
		{
			*ppServer = *(ppServer+1);
		}

		pGroup->active_count--;
		pGroup->chg_count++;

#ifdef WITH_HTTPD
		if (g_http_check_interval <= 0)
		{
			pGroup->http_server_count = pGroup->active_count;
		}
#endif

	}

	tracker_mem_find_store_server(pGroup);

	if ((result=pthread_mutex_unlock(&mem_thread_lock)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call pthread_mutex_unlock fail, " \
			"errno: %d, error info: %s", \
			__LINE__, result, STRERROR(result));
		return result;
	}

	return 0;
}

int tracker_mem_active_store_server(FDFSGroupInfo *pGroup, \
			FDFSStorageDetail *pTargetServer) 
{
	int result;
	FDFSStorageDetail **ppStorageServer;

	if ((pTargetServer->status == FDFS_STORAGE_STATUS_WAIT_SYNC) || \
		(pTargetServer->status == FDFS_STORAGE_STATUS_SYNCING) || \
		(pTargetServer->status == FDFS_STORAGE_STATUS_IP_CHANGED) || \
		(pTargetServer->status == FDFS_STORAGE_STATUS_INIT))
	{
		return 0;
	}

	/*
	if (pTargetServer->status == FDFS_STORAGE_STATUS_DELETED)
	{
		logError("file: "__FILE__", line: %d, " \
			"storage ip: %s already deleted, you can " \
			"restart the tracker servers to reset.", \
			__LINE__, pTargetServer->ip_addr);
		return EAGAIN;
	}
	*/

	pTargetServer->status = FDFS_STORAGE_STATUS_ACTIVE;

	ppStorageServer = (FDFSStorageDetail **)bsearch(&pTargetServer, \
			pGroup->active_servers, \
			pGroup->active_count,   \
			sizeof(FDFSStorageDetail *), \
			tracker_mem_cmp_by_ip_addr);
	if (ppStorageServer != NULL)
	{
		return 0;
	}

	if ((result=pthread_mutex_lock(&mem_thread_lock)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call pthread_mutex_lock fail, " \
			"errno: %d, error info: %s", \
			__LINE__, result, STRERROR(result));
		return result;
	}

	ppStorageServer = (FDFSStorageDetail **)bsearch(&pTargetServer, \
			pGroup->active_servers, \
			pGroup->active_count,   \
			sizeof(FDFSStorageDetail *), \
			tracker_mem_cmp_by_ip_addr);
	if (ppStorageServer == NULL)
	{
		tracker_mem_insert_into_sorted_servers( \
			pTargetServer, pGroup->active_servers, \
			pGroup->active_count);
		pGroup->active_count++;
		pGroup->chg_count++;

#ifdef WITH_HTTPD
		if (g_http_check_interval <= 0)
		{
			pGroup->http_server_count = pGroup->active_count;
		}
#endif

		logDebug("file: "__FILE__", line: %d, " \
			"storage server %s::%s now active", \
			__LINE__, pGroup->group_name, \
			pTargetServer->ip_addr);
	}

	tracker_mem_find_store_server(pGroup);

	if ((result=pthread_mutex_unlock(&mem_thread_lock)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call pthread_mutex_unlock fail, " \
			"errno: %d, error info: %s", \
			__LINE__, result, STRERROR(result));
		return result;
	}

	return 0;
}

int tracker_mem_offline_store_server(FDFSGroupInfo *pGroup, \
			FDFSStorageDetail *pStorage)
{
	pStorage->up_time = 0;
	if ((pStorage->status == FDFS_STORAGE_STATUS_WAIT_SYNC) || \
		(pStorage->status == FDFS_STORAGE_STATUS_SYNCING) || \
		(pStorage->status == FDFS_STORAGE_STATUS_INIT) || \
		(pStorage->status == FDFS_STORAGE_STATUS_DELETED) || \
		(pStorage->status == FDFS_STORAGE_STATUS_IP_CHANGED) || \
		(pStorage->status == FDFS_STORAGE_STATUS_RECOVERY))
	{
		return 0;
	}

	logDebug("file: "__FILE__", line: %d, " \
		"storage server %s::%s offline", \
		__LINE__, pGroup->group_name, \
		pStorage->ip_addr);

	pStorage->status = FDFS_STORAGE_STATUS_OFFLINE;
	return tracker_mem_deactive_store_server(pGroup, pStorage);
}

FDFSStorageDetail *tracker_get_writable_storage(FDFSGroupInfo *pStoreGroup)
{
	int write_server_index;
	if (g_groups.store_server == FDFS_STORE_SERVER_ROUND_ROBIN)
	{
		write_server_index = pStoreGroup->current_write_server++;
		if (pStoreGroup->current_write_server >= \
				pStoreGroup->active_count)
		{
			pStoreGroup->current_write_server = 0;
		}

		if (write_server_index >= pStoreGroup->active_count)
		{
			write_server_index = 0;
		}
		return  *(pStoreGroup->active_servers + write_server_index);
	}
	else //use the first server
	{
		return pStoreGroup->pStoreServer;
	}
}

int tracker_mem_get_storage_by_filename(const byte cmd,FDFS_DOWNLOAD_TYPE_PARAM\
	const char *group_name, const char *filename, const int filename_len, \
	FDFSGroupInfo **ppGroup, FDFSStorageDetail **ppStoreServers, \
	int *server_count)
{
	char szIpAddr[IP_ADDRESS_SIZE];
	FDFSStorageDetail *pStoreSrcServer;
	FDFSStorageDetail **ppServer;
	FDFSStorageDetail **ppServerEnd;
	FDFSStorageDetail *pGroupStoreServer;
	int file_timestamp;
	int storage_ip;
	int read_server_index;
	struct in_addr ip_addr;
	time_t current_time;
	bool bNormalFile;

	*server_count = 0;
	*ppGroup = tracker_mem_get_group(group_name);
	if (*ppGroup == NULL)
	{
		logError("file: "__FILE__", line: %d, " \
			"invalid group_name: %s", \
			__LINE__, group_name);
		return ENOENT;
	}

#ifdef WITH_HTTPD
	if (download_type == FDFS_DOWNLOAD_TYPE_TCP)
	{
		if ((*ppGroup)->active_count == 0)
		{
			return ENOENT;
		}
	}
	else
	{
		if ((*ppGroup)->http_server_count == 0)
		{
			return ENOENT;
		}
	}
#else
	if ((*ppGroup)->active_count == 0)
	{
		return ENOENT;
	}
#endif

	pGroupStoreServer = (*ppGroup)->pStoreServer;
	if (pGroupStoreServer == NULL)
	{
		return ENOENT;
	}

	//file generated by version < v1.12
	if (filename_len < 32 + (FDFS_FILE_EXT_NAME_MAX_LEN + 1))
	{
		storage_ip = INADDR_NONE;
		file_timestamp = 0;
		bNormalFile = true;
	}
	else //file generated by version >= v1.12
	{
		int64_t file_size;

		char name_buff[64];
		int decoded_len;

		base64_decode_auto(&g_base64_context, (char *)filename + \
			FDFS_LOGIC_FILE_PATH_LEN, FDFS_FILENAME_BASE64_LENGTH, \
			name_buff, &decoded_len);
		storage_ip = ntohl(buff2int(name_buff));
		file_timestamp = buff2int(name_buff+sizeof(int));
		file_size = buff2long(name_buff + sizeof (int) * 2);

		bNormalFile = !(IS_SLAVE_FILE(filename_len) || \
				IS_APPENDER_FILE(file_size));
	}

	//logInfo("storage_ip=%d, file_timestamp=%d\n", storage_ip,file_timestamp);

	memset(szIpAddr, 0, sizeof(szIpAddr));
	if (cmd == TRACKER_PROTO_CMD_SERVICE_QUERY_FETCH_ONE)
	{
		if (g_groups.download_server == \
				FDFS_DOWNLOAD_SERVER_SOURCE_FIRST)
		{
			memset(&ip_addr, 0, sizeof(ip_addr));
			ip_addr.s_addr = storage_ip;
#ifdef WITH_HTTPD
			if (download_type == FDFS_DOWNLOAD_TYPE_TCP)
			{
			pStoreSrcServer=tracker_mem_get_active_storage( \
				*ppGroup, inet_ntop(AF_INET, &ip_addr, \
				szIpAddr, sizeof(szIpAddr)));
			}
			else
			{
			pStoreSrcServer=tracker_mem_get_active_http_server( \
				*ppGroup, inet_ntop(AF_INET, &ip_addr, \
				szIpAddr, sizeof(szIpAddr)));
			}
#else
			pStoreSrcServer=tracker_mem_get_active_storage(\
				*ppGroup, inet_ntop(AF_INET, &ip_addr, \
				szIpAddr, sizeof(szIpAddr)));
#endif
			if (pStoreSrcServer != NULL)
			{
				ppStoreServers[(*server_count)++] = \
						 pStoreSrcServer;
				return 0;
			}
		}

		//round robin
#ifdef WITH_HTTPD
		if (download_type == FDFS_DOWNLOAD_TYPE_TCP)
		{
		read_server_index = (*ppGroup)->current_read_server;
		if (read_server_index >= (*ppGroup)->active_count)
		{
			read_server_index = 0;
		}
		ppStoreServers[(*server_count)++]=*((*ppGroup)->active_servers \
				+ read_server_index);
		}
		else
		{
		read_server_index = (*ppGroup)->current_http_server;
		if (read_server_index >= (*ppGroup)->http_server_count)
		{
			read_server_index = 0;
		}
		ppStoreServers[(*server_count)++]=*((*ppGroup)->http_servers \
				+ read_server_index);
		}
#else
		read_server_index = (*ppGroup)->current_read_server;
		if (read_server_index >= (*ppGroup)->active_count)
		{
			read_server_index = 0;
		}
		ppStoreServers[(*server_count)++]=*((*ppGroup)->active_servers \
				+ read_server_index);
#endif

		/*
		//logInfo("filename=%s, pStorageServer ip=%s, " \
		"file_timestamp=%d, " \
		"last_synced_timestamp=%d\n", filename, \
		ppStoreServers[0]->ip_addr, file_timestamp, \
		(int)ppStoreServers[0]->stat.last_synced_timestamp);
		 */
		do
		{
			if (bNormalFile)
			{
			current_time = time(NULL);
			if ((file_timestamp < current_time - \
				g_storage_sync_file_max_delay) || \
			(ppStoreServers[0]->stat.last_synced_timestamp > \
				file_timestamp) || \
			(ppStoreServers[0]->stat.last_synced_timestamp + 1 >= \
			 file_timestamp && current_time - file_timestamp > \
				g_storage_sync_file_max_time)\
			|| (storage_ip == INADDR_NONE \
			&& g_groups.store_server == FDFS_STORE_SERVER_ROUND_ROBIN))
			{
				break;
			}

			if (storage_ip == INADDR_NONE)
			{
#ifdef WITH_HTTPD
				if (download_type == FDFS_DOWNLOAD_TYPE_TCP)
				{
				ppStoreServers[0] = pGroupStoreServer;
				break;
				}
				else
				{
				pStoreSrcServer=tracker_mem_get_active_storage(
				*ppGroup, pGroupStoreServer->ip_addr);
				if (pStoreSrcServer != NULL)
				{
					ppStoreServers[0] = pStoreSrcServer;
					break;
				}
				}
#else
				ppStoreServers[0] = pGroupStoreServer;
				break;
#endif
			}
			}

			memset(&ip_addr, 0, sizeof(ip_addr));
			ip_addr.s_addr = storage_ip;
			inet_ntop(AF_INET, &ip_addr, \
				szIpAddr, sizeof(szIpAddr));
			if (strcmp(szIpAddr, ppStoreServers[0]->ip_addr) == 0)
			{
#ifdef WITH_HTTPD
				if (download_type == FDFS_DOWNLOAD_TYPE_TCP)
				{
					break;
				}
				else //http
				{
					if (tracker_mem_get_active_http_server(
						*ppGroup, szIpAddr) != NULL)
					{
						break;
					}
				}
#else
				break;
#endif
			}

			if (g_groups.download_server == \
					FDFS_DOWNLOAD_SERVER_ROUND_ROBIN)
			{  //avoid search again
#ifdef WITH_HTTPD
				if (download_type == FDFS_DOWNLOAD_TYPE_TCP)
				{
				pStoreSrcServer=tracker_mem_get_active_storage(
					*ppGroup, szIpAddr);
				}
				else //http
				{
				pStoreSrcServer=tracker_mem_get_active_http_server(
					*ppGroup, szIpAddr);
				}
#else
				pStoreSrcServer=tracker_mem_get_active_storage(
					*ppGroup, szIpAddr);
#endif
				if (pStoreSrcServer != NULL)
				{
					ppStoreServers[0] = pStoreSrcServer;
					break;
				}
			}

			if (g_groups.store_server != \
				FDFS_DOWNLOAD_SERVER_ROUND_ROBIN)
			{
#ifdef WITH_HTTPD
				if (download_type == FDFS_DOWNLOAD_TYPE_TCP)
				{
				ppStoreServers[0] = pGroupStoreServer;
				}
				else  //http
				{
				pStoreSrcServer=tracker_mem_get_active_http_server(
					*ppGroup, pGroupStoreServer->ip_addr);
				if (pStoreSrcServer != NULL)
				{
				ppStoreServers[0] = pStoreSrcServer;
				}
				else
				{
				ppStoreServers[0] = *((*ppGroup)->http_servers);
				}
				}
#else
				ppStoreServers[0] = pGroupStoreServer;
#endif
				break;
			}
		} while (0);

#ifdef WITH_HTTPD
		if (download_type == FDFS_DOWNLOAD_TYPE_TCP)
		{
		(*ppGroup)->current_read_server++;
		if ((*ppGroup)->current_read_server >= (*ppGroup)->active_count)
		{
			(*ppGroup)->current_read_server = 0;
		}
		}
		else  //http
		{
			(*ppGroup)->current_http_server++;
			if ((*ppGroup)->current_http_server >= \
				(*ppGroup)->http_server_count)
			{
				(*ppGroup)->current_http_server = 0;
			}
		}
#else
		(*ppGroup)->current_read_server++;
		if ((*ppGroup)->current_read_server >= (*ppGroup)->active_count)
		{
			(*ppGroup)->current_read_server = 0;
		}
#endif
	}
	else if (cmd == TRACKER_PROTO_CMD_SERVICE_QUERY_UPDATE)
	{
		if (storage_ip != INADDR_NONE)
		{
			memset(&ip_addr, 0, sizeof(ip_addr));
			ip_addr.s_addr = storage_ip;
			pStoreSrcServer=tracker_mem_get_active_storage(\
					*ppGroup, inet_ntop(AF_INET, &ip_addr, \
					szIpAddr, sizeof(szIpAddr)));
			if (pStoreSrcServer != NULL)
			{
				ppStoreServers[(*server_count)++] = \
							 pStoreSrcServer;
				return 0;
			}
		}

		ppStoreServers[0] = tracker_get_writable_storage(*ppGroup);
		*server_count = ppStoreServers[0] != NULL ? 1 : 0;
	}
	else //TRACKER_PROTO_CMD_SERVICE_QUERY_FETCH_ALL
	{
		memset(szIpAddr, 0, sizeof(szIpAddr));
		if (storage_ip != INADDR_NONE)
		{
			memset(&ip_addr, 0, sizeof(ip_addr));
			ip_addr.s_addr = storage_ip;
			inet_ntop(AF_INET, &ip_addr,szIpAddr,sizeof(szIpAddr));
		}

		if (bNormalFile)
		{
		current_time = time(NULL);
		ppServerEnd = (*ppGroup)->active_servers + \
				(*ppGroup)->active_count;

		for (ppServer=(*ppGroup)->active_servers; \
				ppServer<ppServerEnd; ppServer++)
		{
			if ((file_timestamp < current_time - \
				g_storage_sync_file_max_delay) || \
			((*ppServer)->stat.last_synced_timestamp > \
				file_timestamp) || \
			((*ppServer)->stat.last_synced_timestamp + 1 >= \
			 file_timestamp && current_time - file_timestamp > \
				g_storage_sync_file_max_time) \
				|| (storage_ip == INADDR_NONE \
					&& g_groups.store_server == \
					FDFS_STORE_SERVER_ROUND_ROBIN)
				|| strcmp((*ppServer)->ip_addr, szIpAddr) == 0)
			{
				ppStoreServers[(*server_count)++] = *ppServer;
			}
		}
		}
		else
		{
			pStoreSrcServer = tracker_mem_get_active_storage( \
						*ppGroup, szIpAddr);
			if (pStoreSrcServer != NULL)
			{
				ppStoreServers[(*server_count)++] = \
						pStoreSrcServer;
			}
		}

		if (*server_count == 0)
		{
			ppStoreServers[(*server_count)++] = pGroupStoreServer;
		}
	}

	return *server_count > 0 ? 0 : ENOENT;
}

int tracker_mem_check_alive(void *arg)
{
	FDFSStorageDetail **ppServer;
	FDFSStorageDetail **ppServerEnd;
	FDFSGroupInfo **ppGroup;
	FDFSGroupInfo **ppGroupEnd;
	FDFSStorageDetail *deactiveServers[FDFS_MAX_SERVERS_EACH_GROUP];
	int deactiveCount;
	time_t current_time;

	current_time = time(NULL);
	ppGroupEnd = g_groups.groups + g_groups.count;
	for (ppGroup=g_groups.groups; ppGroup<ppGroupEnd; ppGroup++)
	{
	deactiveCount = 0;
	ppServerEnd = (*ppGroup)->active_servers + (*ppGroup)->active_count;
	for (ppServer=(*ppGroup)->active_servers; ppServer<ppServerEnd; ppServer++)
	{
		if (current_time - (*ppServer)->stat.last_heart_beat_time > \
			g_check_active_interval)
		{
			deactiveServers[deactiveCount] = *ppServer;
			deactiveCount++;
			if (deactiveCount >= FDFS_MAX_SERVERS_EACH_GROUP)
			{
				break;
			}
		}
	}

	if (deactiveCount == 0)
	{
		continue;
	}

	ppServerEnd = deactiveServers + deactiveCount;
	for (ppServer=deactiveServers; ppServer<ppServerEnd; ppServer++)
	{
	(*ppServer)->status = FDFS_STORAGE_STATUS_OFFLINE;
	tracker_mem_deactive_store_server(*ppGroup, *ppServer);
	logInfo("ip=%s idle too long, status change to offline!", (*ppServer)->ip_addr);
	}
	}

	return 0;
}

int tracker_mem_get_storage_index(FDFSGroupInfo *pGroup, \
		FDFSStorageDetail *pStorage)
{
	FDFSStorageDetail **ppStorage;
	FDFSStorageDetail **ppEnd;

	ppEnd = pGroup->all_servers + pGroup->count;
	for (ppStorage=pGroup->all_servers; ppStorage<ppEnd; ppStorage++)
	{
		if (*ppStorage == pStorage)
		{
			return ppStorage - pGroup->all_servers;
		}
	}

	logError("file: "__FILE__", line: %d, " \
		"get index of storage %s fail!!!", \
		__LINE__, pStorage->ip_addr);

	return -1;
}

