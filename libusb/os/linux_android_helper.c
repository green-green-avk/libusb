/*
 * linux_android_helper.c
 *
 *  Created on: Oct 18, 2019
 *      Author: alex
 */

#include "config.h"

#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

#include "libusbi.h"
#include "linux_usbfs.h"

/* <=---=> */

/* Hello, Musl C! (Alpine Linux at least) */

/* Evaluate EXPRESSION, and repeat as long as it returns -1 with `errno'
   set to EINTR.  */

#ifndef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY(expression) \
  (__extension__							      \
    ({ long int __result;						      \
       do __result = (long int) (expression);				      \
       while (__result == -1L && errno == EINTR);			      \
       __result; }))
#endif

/* <=---=> */

static ssize_t readAll(const int fd, void *data, size_t len) {
	ssize_t r = 0;
	do {
		data += r;
		len -= r;
		r = TEMP_FAILURE_RETRY(read(fd, data, len));
	} while (r > 0 && r < len);
	return r;
}

static void closeFds(const int *const fds, size_t cnt) {
	while (cnt--)
		close(fds[cnt]);
}

static ssize_t recvFds(const int sockfd, void *const data, const size_t len,
		int *const fds, size_t *const fds_count) {
	const size_t cmsg_space = CMSG_SPACE(sizeof(int) * *fds_count);
	if (cmsg_space >= getpagesize()) {
		errno = ENOMEM;
		return -1;
	}
	char cmsg_buf[cmsg_space] __attribute__((aligned(_Alignof(struct cmsghdr))));
	struct iovec iov = { .iov_base = data, .iov_len = len };
	struct msghdr msg = { .msg_name = NULL, .msg_namelen = 0, .msg_iov = &iov,
			.msg_iovlen = 1, .msg_control = cmsg_buf,
			// We can't cast to the actual type of the field, because it's different across platforms.
			.msg_controllen = (unsigned int) (cmsg_space), .msg_flags = 0, };
	int flags = MSG_TRUNC | MSG_CTRUNC;
#if defined(__linux__)
	flags |= MSG_CMSG_CLOEXEC | MSG_NOSIGNAL;
#endif
	ssize_t rc = TEMP_FAILURE_RETRY(recvmsg(sockfd, &msg, flags));
	if (rc == -1) {
		return -1;
	}
	if ((msg.msg_flags & MSG_TRUNC)) {
		usbi_err(NULL, "message was truncated when receiving file descriptors");
		errno = EMSGSIZE;
		return -1;
	} else if ((msg.msg_flags & MSG_CTRUNC)) {
		usbi_err(NULL,
				"control message was truncated when receiving file descriptors");
		errno = EMSGSIZE;
		return -1;
	}
	size_t out_fd_i = 0;
	struct cmsghdr *cmsg;
	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
			cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
			usbi_err(NULL, "received unexpected cmsg: [%d, %d]",
					cmsg->cmsg_level, cmsg->cmsg_type);
			closeFds(fds, out_fd_i);
			errno = EBADMSG;
			return -1;
		}
		// There isn't a macro that does the inverse of CMSG_LEN, so hack around it ourselves, with
		// some asserts to ensure that CMSG_LEN behaves as we expect.
#if defined(__linux__)
#define CMSG_ASSERT(V) _Static_assert((V), "Assertion failed")
#else
// CMSG_LEN is somehow not constexpr on darwin.
#define CMSG_ASSERT CHECK
#endif
		CMSG_ASSERT(CMSG_LEN(0) + 1 * sizeof(int) == CMSG_LEN(1 * sizeof(int)));
		CMSG_ASSERT(CMSG_LEN(0) + 2 * sizeof(int) == CMSG_LEN(2 * sizeof(int)));
		CMSG_ASSERT(CMSG_LEN(0) + 3 * sizeof(int) == CMSG_LEN(3 * sizeof(int)));
		CMSG_ASSERT(CMSG_LEN(0) + 4 * sizeof(int) == CMSG_LEN(4 * sizeof(int)));
		if (cmsg->cmsg_len % sizeof(int) != 0) {
			usbi_err(NULL, "cmsg_len(%lu) not aligned to sizeof(int)",
					cmsg->cmsg_len);
		} else if (cmsg->cmsg_len <= CMSG_LEN(0)) {
			usbi_err(NULL, "cmsg_len(%lu) not long enough to hold any data",
					cmsg->cmsg_len);
		}
		int *const cmsg_fds = (int*) (CMSG_DATA(cmsg));
		const size_t cmsg_fdcount = (size_t) (cmsg->cmsg_len - CMSG_LEN(0))
				/ sizeof(int);
		if (out_fd_i + cmsg_fdcount > *fds_count) {
			usbi_err(NULL,
					"received too many file descriptors, expected %lu, received %lu",
					*fds_count, out_fd_i + cmsg_fdcount);
			closeFds(fds, out_fd_i);
			errno = EMSGSIZE;
			return -1;
		}
		for (size_t i = 0; i < cmsg_fdcount; ++i, ++out_fd_i) {
#if !defined(__linux__)
			// Linux uses MSG_CMSG_CLOEXEC instead of doing this manually.
			fcntl(cmsg_fds[i], F_SETFD, FD_CLOEXEC);
#endif
			fds[out_fd_i] = cmsg_fds[i];
		}
	}
	*fds_count = out_fd_i;
	return rc;
}

static int sendStr(const int fd, const char *const str) {
	const uint16_t len = strlen(str);
	const uint16_t n_len = htons(len);
	if (write(fd, &n_len, 2) < 2)
		return -1;
	if (len > 0)
		if (write(fd, str, len) < len)
			return -1;
	return 0;
}

static int recvStr(const int fd, char *const buf, const int size) {
	uint16_t n_len;
	ssize_t r = readAll(fd, &n_len, sizeof(n_len));
	const uint16_t len = ntohs(n_len);
	if (r != 2 || len >= size)
		return -1;
	if (len == 0) {
		buf[0] = '\0';
		return 0;
	}
	r = readAll(fd, buf, len);
	buf[len] = '\0';
	if (r <= 0)
		return -1;
	return len;
}

static int servConnOpen(void) {
	const char socketName[] = "\0"
	USE_ANDROID_LIBUSB_HELPER;
	const int sock = socket(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (sock < 0) {
		usbi_err(NULL, "Can't create socket: errno=%d", errno);
		return -1;
	}
	struct sockaddr_un sockAddr;
	sockAddr.sun_family = AF_LOCAL;
	memcpy(sockAddr.sun_path, socketName, sizeof(socketName));
	if (connect(sock, (struct sockaddr*) &sockAddr,
			sizeof(socketName) - 1 + offsetof(struct sockaddr_un, sun_path))
			< 0) {
		close(sock);
		usbi_err(NULL, "Can't connect to libusb helper server: errno=%d",
				errno);
		return -1;
	}
	return sock;
}

static int servConnClose(const int sock) {
	return close(sock);
}

int linux_android_helper_get_usbfs_fd(const struct libusb_device *const dev,
		const int silent) {
	const int sock = servConnOpen();
	char name[PATH_MAX];
	snprintf(name, sizeof(name), "/dev/bus/usb/%03u/%03u", dev->bus_number,
			dev->device_address);
	if (sendStr(sock, name) < 0)
		return LIBUSB_ERROR_IO;
	char result;
	int fd;
	size_t fdsNum = 1;
	if (recvFds(sock, &result, sizeof(result), &fd, &fdsNum) < 1 || fdsNum != 1)
		return LIBUSB_ERROR_IO;
	close(sock);
	/*
	 * We must rewind the file descriptor because the Android USB subsystem
	 * also caches the device descriptor from it.
	 */
	if (lseek(fd, 0, SEEK_SET) < 0) {
		usbi_err(NULL, "seek failed errno=%d", errno);
		return LIBUSB_ERROR_IO;
	}
	return fd;
}

#if 0
int linux_android_helper_open(struct libusb_device *dev) {
	return -1;
}

int linux_android_helper_close(struct libusb_device *dev) {
	return -1;
}
#endif

static int on_dev_event(const char *devName, const int detach,
		const int notEvent) {
	uint8_t busnum;
	uint8_t devaddr;
	int r = linux_get_device_address(NULL, 1, &busnum, &devaddr, devName, NULL,
			-1);
	if (r != LIBUSB_SUCCESS)
		return r;
	if (detach)
		linux_device_disconnected(busnum, devaddr);
	else
		linux_hotplug_enumerate(busnum, devaddr, NULL);
	return LIBUSB_SUCCESS;
}

typedef enum {
	DEV_ATTACHED = 0, DEV_DETACHED = 1
} dev_act_t;

typedef struct {
	dev_act_t action;
	char dev_name[PATH_MAX];
} new_dev_evt_t;

static int getDevList(const int fd) {
	char devName[PATH_MAX];
	int r;
	while ((r = recvStr(fd, devName, sizeof(devName))) > 0) {
		on_dev_event(devName, 0, 1);
	}
	return r;
}

static int getNewDevEvent(const int fd, new_dev_evt_t *const newDev) {
	int r;
	uint8_t action;
	r = readAll(fd, &action, sizeof(action));
	if (r != 1)
		return -1;
	r = recvStr(fd, newDev->dev_name, sizeof(newDev->dev_name));
	newDev->action = action;
	if (r < 0)
		return -1;
	return 0;
}

static pthread_t event_th;
static int event_fd = -1;
static int event_ctl_pipe[2] = { -1, -1 };

static void* event_thread_main(void *arg) {
	new_dev_evt_t new_dev;
	struct pollfd pfds[] =
			{ { .fd = event_ctl_pipe[0], .events = POLLIN, .revents = 0 }, {
					.fd = event_fd, .events = POLLIN, .revents = 0 }, };
	while (TEMP_FAILURE_RETRY(poll(pfds, 2, -1)) > 0) {
		if (pfds[1].revents == 0) {
			usbi_dbg("Event monitor exit");
			return NULL;
		}
		int r = getNewDevEvent(event_fd, &new_dev);
		if (r < 0) {
			usbi_err(NULL, "Event protocol error");
			return NULL;
		}
		on_dev_event(new_dev.dev_name, new_dev.action == DEV_DETACHED, 0);
	}
	usbi_err(NULL, "Event monitor poll error");
	return NULL;
}

int linux_android_helper_start_event_monitor(void) {
	event_fd = servConnOpen();
	if (event_fd == -1)
		return LIBUSB_ERROR_OTHER;
	int r;
	r = sendStr(event_fd, "");
	if (r) {
		usbi_err(NULL, "connecting to the event service");
		goto error_after_serv;
	}
	r = usbi_pipe(event_ctl_pipe);
	if (r) {
		usbi_err(NULL, "creating event control pipe (%d)", r);
		goto error_after_serv;
	}
	r = getDevList(event_fd);
	if (r) {
		usbi_err(NULL, "getting device list (%d)", r);
		goto error_after_pipe;
	}
	r = pthread_create(&event_th, NULL, event_thread_main, NULL);
	if (r) {
		usbi_err(NULL, "creating hotplug event thread (%d)", r);
		goto error_after_pipe;
	}
	return LIBUSB_SUCCESS;
error_after_pipe:
	close(event_ctl_pipe[1]);
	event_ctl_pipe[1] = -1;
	close(event_ctl_pipe[0]);
	event_ctl_pipe[0] = -1;
error_after_serv:
	servConnClose(event_fd);
	event_fd = -1;
	return LIBUSB_ERROR_OTHER;
}

int linux_android_helper_stop_event_monitor(void) {
	close(event_ctl_pipe[1]);
	event_ctl_pipe[1] = -1;
	pthread_join(event_th, NULL);
	close(event_ctl_pipe[0]);
	event_ctl_pipe[0] = -1;
	servConnClose(event_fd);
	event_fd = -1;
	return LIBUSB_SUCCESS;
}
