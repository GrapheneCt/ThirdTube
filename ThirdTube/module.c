#include <kernel.h>
#include <apputil.h>
#include <libhttp.h>
#include <stdlib.h>
#include <string.h>

extern SceInt32 g_httpTemplate;

int module_stop(SceSize argc, const void *args)
{
	if (g_httpTemplate >= 0)
		sceHttpDeleteTemplate(g_httpTemplate);

	return SCE_KERNEL_STOP_SUCCESS;
}

int module_exit()
{
	return SCE_KERNEL_STOP_SUCCESS;
}

int cleanPath(const char *path)
{
	SceUID dfd = sceIoDopen(path);
	if (dfd >= 0) {
		int res = SCE_OK;

		do {
			SceIoDirent dir;
			sceClibMemset(&dir, 0, sizeof(SceIoDirent));

			res = sceIoDread(dfd, &dir);
			if (res > 0) {
				char *new_path = malloc(strlen(path) + strlen(dir.d_name) + 2);
				sceClibSnprintf(new_path, 1024, "%s/%s", path, dir.d_name);

				/*
				if (SCE_S_ISDIR(dir.d_stat.st_mode)) {
					int ret = removePath(new_path);
					if (ret <= 0) {
						free(new_path);
						sceIoDclose(dfd);
						return ret;
					}
				}
				else */
				{
					int ret = sceIoRemove(new_path);
					if (ret < 0) {
						free(new_path);
						sceIoDclose(dfd);
						return ret;
					}
				}

				free(new_path);
			}
		} while (res > 0);

		sceIoDclose(dfd);

		/*
		int ret = sceIoRmdir(path);
		if (ret < 0)
			return ret;
		*/
	}
	else {
		int ret = SCE_OK;
		/*
		ret = sceIoRemove(path);
		if (ret < 0)
		*/
			return ret;
	}

	return SCE_OK;
}

int module_start(SceSize argc, void *args)
{
	int ret = -1;
	/*SceSize usedDataSize = 0;
	SceAppUtilMountPoint mount;
	SceAppUtilInitParam iparam;
	SceAppUtilBootParam bparam;*/
	SceUID fd;

	fd = sceIoDopen("savedata0:yt_json_cache");
	if (fd <= 0) {
		sceIoMkdir("savedata0:yt_json_cache", 0666);
	}
	else
		sceIoDclose(fd);

	/*sceClibSnprintf((char *)mount.data, sizeof(mount.data), "savedata0:");

	ret = sceAppUtilSaveDataGetQuota(NULL, &usedDataSize, &mount);
	if (ret == SCE_APPUTIL_ERROR_NOT_INITIALIZED) {
		sceClibMemset(&iparam, 0, sizeof(SceAppUtilInitParam));
		sceClibMemset(&bparam, 0, sizeof(SceAppUtilBootParam));
		sceAppUtilInit(&iparam, &bparam);
		sceAppUtilSaveDataGetQuota(NULL, &usedDataSize, &mount);
	}

	if (usedDataSize > 1 * 1024 * 1024)
		cleanPath("savedata0:yt_json_cache");*/

	return SCE_KERNEL_START_SUCCESS;
}