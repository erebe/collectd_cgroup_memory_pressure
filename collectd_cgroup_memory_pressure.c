#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/eventfd.h>
#include <sys/epoll.h>

#define INTERVAL_TIME 30
#define MAX_EVENTS 1
#define USAGE_STR "Usage: collectd_cgroup_memory_pressure <path-to-control-file> <memory level> <collectd_path>"

// https://www.kernel.org/doc/Documentation/cgroup-v1/memory.txt chapter 11
int main(int argc, char **argv)
{
	int efd = -1;
	int cfd = -1;
	int event_control = -1;
	char event_control_path[PATH_MAX];
	char line[LINE_MAX];
	int ret;

	if (argc != 4) errx(1, "%s", USAGE_STR);

        char* interval_time_str = getenv("COLLECTD_INTERVAL");
        int interval_time = (interval_time_str != NULL)
                          ? atoi(interval_time_str)
                          : INTERVAL_TIME;

        ret = snprintf(event_control_path, PATH_MAX, "%s/memory.pressure_level", argv[1]);
	if (ret >= PATH_MAX) errx(1, "Path to memory.pressure_level is too long");

	cfd = open(event_control_path, O_RDONLY);
	if (cfd == -1) err(1, "Cannot open %s", event_control_path);

	ret = snprintf(event_control_path, PATH_MAX, "%s/cgroup.event_control", argv[1]);
	if (ret >= PATH_MAX) errx(1, "Path to cgroup.event_control is too long");

	event_control = open(event_control_path, O_WRONLY);
	if (event_control == -1) err(1, "Cannot open %s", event_control_path);

	efd = eventfd(0, 0);
	if (efd == -1) err(1, "eventfd() failed");

	ret = snprintf(line, LINE_MAX, "%d %d %s", efd, cfd, argv[2]);
	if (ret >= LINE_MAX) errx(1, "Arguments string is too long");

	ret = write(event_control, line, strlen(line) + 1);
	if (ret == -1) err(1, "Cannot write to cgroup.event_control");

        int epollfd = epoll_create1(0);
	if (epollfd == -1) err(1, "Cannot create event loop");

        struct epoll_event ev, events[MAX_EVENTS];
        ev.events = EPOLLIN;
        ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, efd, &ev);
        if (ret == -1) err(1, "Cannot register event loop on memory.pressure_level");

        uint64_t result;
	for(;;) {
            ret = epoll_wait(epollfd, events, MAX_EVENTS,  1000 * interval_time);
            switch(ret) {
              fail:
              case -1:
                  printf("PUTVAL \"%s\" interval=%d N:-1\n", argv[3], interval_time);
                  break;

              case 0:
                  printf("PUTVAL \"%s\" interval=%d N:0\n", argv[3], interval_time);
                  break;

              default:
	          ret = read(efd, &result, sizeof(result));
                  if(ret < sizeof(result)) goto fail;
	          ret = access(event_control_path, W_OK);
                  if(ret < 0) goto fail;

                  printf("PUTVAL \"%s\" interval=%d N:1\n", argv[3], interval_time);
                  break;
            }
	}

	return EXIT_SUCCESS;
}
