/*************************************************************************
	> File Name: test.c
	> Author: bxq
	> Mail: 544177215@qq.com 
	> Created Time: Saturday, December 12, 2015 PM03:19:12 CST
 ************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include "rtsp_demo.h"

static int flag_run = 1;
static void sig_proc(int signo)
{
	flag_run = 0;
}

static int get_next_video_frame (FILE *fp, uint8_t **buff, int *size)
{
	uint8_t szbuf[1024];
	int szlen = 0;
	int ret;
	if (!(*buff)) {
		*buff = (uint8_t*)malloc(2*1024*1024);
		if (!(*buff))
			return -1;
	}

	*size = 0;

	while ((ret = fread(szbuf + szlen, 1, sizeof(szbuf) - szlen, fp)) > 0) {
		int i = 3;
		szlen += ret;
		while (i < szlen - 3 && !(szbuf[i] == 0 &&  szbuf[i+1] == 0 && (szbuf[i+2] == 1 || (szbuf[i+2] == 0 && szbuf[i+3] == 1)))) i++;
		memcpy(*buff + *size, szbuf, i);
		*size += i;
		memmove(szbuf, szbuf + i, szlen - i);
		szlen -= i;
		if (szlen > 3) {
			//printf("szlen %d\n", szlen);
			fseek(fp, -szlen, SEEK_CUR);
			break;
		}
	}
	if (ret > 0)
		return *size;
	return 0;
}

static int get_next_audio_frame (FILE *fp, uint8_t **buff, int *size)
{
	int ret;
#define AUDIO_FRAME_SIZE 320
	if (!(*buff)) {
		*buff = (uint8_t*)malloc(AUDIO_FRAME_SIZE);
		if (!(*buff))
			return -1;
	}

	ret = fread(*buff, 1, AUDIO_FRAME_SIZE, fp);
	if (ret > 0) {
		*size = ret;
		return ret;
	}
	return 0;
}

#define MAX_SESSION_NUM 64
#define DEMO_CFG_FILE "demo.cfg"

struct demo_cfg
{
	int session_count;
	struct {
		char path[64];
		char video_file[64];
		char audio_file[64];
	} session_cfg[MAX_SESSION_NUM];
};

static int load_cfg(struct demo_cfg *cfg, const char *cfg_file)
{
//cfgline: path=%s video=%s audio=%s
	FILE *fp = fopen(cfg_file, "r");
	char line[256];
	int count = 0;

	if (!fp) {
		fprintf(stderr, "open %s failed\n", cfg_file);
		return -1;
	}

	memset(cfg, 0, sizeof(*cfg));
	while (fgets(line, sizeof(line) - 1, fp)) {
		const char *p;
		memset(&cfg->session_cfg[count], 0, sizeof(cfg->session_cfg[count]));

		if (line[0] == '#')
			continue;

		p = strstr(line, "path=");
		if (!p)
			continue;
		if (sscanf(p, "path=%s", cfg->session_cfg[count].path) != 1)
			continue;
		if ((p = strstr(line, "video="))) {
			if (sscanf(p, "video=%s", cfg->session_cfg[count].video_file) != 1) {
				fprintf(stderr, "parse video file failed %s\n", p);
			}
		}
		if ((p = strstr(line, "audio="))) {
			if (sscanf(p, "audio=%s", cfg->session_cfg[count].audio_file) != 1) {
				fprintf(stderr, "parse audio file failed %s\n", p);
			}
		}
		if (strlen(cfg->session_cfg[count].video_file) || strlen(cfg->session_cfg[count].audio_file)) {
			count ++;
		} else {
			fprintf(stderr, "parse line %s failed\n", line);
		}
	}
	cfg->session_count = count;
	fclose(fp);
	return count;
}

#ifdef __LINUX__
#include <unistd.h>
#endif
#ifdef __WIN32__
#include <windows.h>
#define usleep(x) Sleep((x)/1000)
#endif

int main(int argc, char *argv[]){
	uint8_t *vbuf = NULL;
	int vsize = 0;
	int ret;

	rtsp_demo_handle g_rtsplive = NULL;
	rtsp_session_handle g_rtsp_session;
	g_rtsplive = rtsp_new_demo(8554);
	g_rtsp_session = rtsp_new_session(g_rtsplive, "/stream");

	
	FILE* fp = fopen("./BarbieGirl.h264", "rb");

	if (!fp) {
		fprintf(stderr, "open file failed\n");
	}

	if(fp){
		rtsp_set_video(g_rtsp_session, RTSP_CODEC_ID_VIDEO_H264, NULL, 0);
		rtsp_sync_video_ts(g_rtsp_session, rtsp_get_reltime(), rtsp_get_ntptime());
	}

	printf("==========> rtsp://127.0.0.1:8554/stream <===========\n");

	signal(SIGINT, sig_proc);
	signal(SIGPIPE, SIG_IGN);
	uint64_t ts = rtsp_get_reltime();

	while(flag_run){
		if (fp) {
			read_video_again:
				ret = get_next_video_frame(fp, &vbuf, &vsize);
				if (ret < 0) {
					fprintf(stderr, "get_next_video_frame failed\n");
					flag_run = 0;
					break;
				}
				if (ret == 0) {
					fseek(fp, 0, SEEK_SET);
					if (fp)
						fseek(fp, 0, SEEK_SET);
					goto read_video_again;
				}

				if (g_rtsp_session)
					rtsp_tx_video(g_rtsp_session, vbuf, vsize, ts);
		}
		
		do {
			ret = rtsp_do_event(g_rtsplive);
			if (ret > 0)
				continue;
			if (ret < 0)
				break;
			usleep(20000);
		} while (rtsp_get_reltime() - ts < 1000000 / 25);
		if (ret < 0)
			break;
		ts += 1000000 / 25;
		printf(".");fflush(stdout);
	}

	free(vbuf);
	fclose(fp);
	if (g_rtsplive) rtsp_del_demo(g_rtsplive);
	return 0;
}
