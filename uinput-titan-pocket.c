#define _GNU_SOURCE
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

long map(long x, long in_min, long in_max, long out_min, long out_max)
{
	return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

//now() is in total us mod 10^15
uint64_t now() {
	uint64_t t;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	t = tv.tv_usec;
	t += (tv.tv_sec % (1000*1000*1000)) * 1000*1000LL;
	return t;
}

static uint64_t lastKbdTimestamp;

static void insertEvent(int fd, unsigned short type, unsigned short code, int value) {
	struct input_event e;
	memset(&e, 0, sizeof(e));
	e.type = type;
	e.code = code;
	e.value = value;
	write(fd, &e, sizeof(e));
}

static int uinput_init() {
	int fd = open("/dev/uinput", O_RDWR);

	struct uinput_user_dev setup = {
		.id = {
			.bustype = BUS_VIRTUAL,
			.vendor = 0xdead,
			.product = 0xbeaf,
			.version = 3,
		},
		.name = "titan pocket uinput",
		.ff_effects_max = 0,
	};

	setup.absmax[ABS_X] = 720 - 1;//width
	setup.absmax[ABS_Y] = 720 - 1; //height
	setup.absmax[ABS_MT_POSITION_X] = 720 - 1;//width
	setup.absmax[ABS_MT_POSITION_Y] = 720 - 1; //height

	setup.absmax[ABS_MT_SLOT] = 1;
	setup.absmax[ABS_MT_TRACKING_ID] = 1;

	write(fd, &setup, sizeof(setup));

	ioctl(fd, UI_SET_EVBIT, EV_ABS);
	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_ABSBIT, ABS_X);
	ioctl(fd, UI_SET_ABSBIT, ABS_Y);
	ioctl(fd, UI_SET_ABSBIT, ABS_MT_SLOT);
	ioctl(fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
	ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
	ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
	ioctl(fd, UI_SET_KEYBIT, KEY_TAB);
	ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);

	const char phys[] = "this/is/a/virtual/device/for/scrolling";
	ioctl(fd, UI_SET_PHYS, phys);
	ioctl(fd, UI_DEV_CREATE, NULL);
	return fd;
}


static int open_ev(const char *lookupName) {
	char *path = NULL;
	for(int i=0; i<64;i++) {
		asprintf(&path, "/dev/input/event%d", i);
		int fd = open(path, O_RDWR);
		if(fd < 0) continue;
		char name[128];
		ioctl(fd, EVIOCGNAME(sizeof(name)), name);
		if(strcmp(name, lookupName) == 0) {
			return fd;
		}

		close(fd);
	}
	free(path);
	return -1;
}

static int original_input_init() {
	int fd = open_ev("mtk-pad");
	if(fd<0) return fd;
	ioctl(fd, EVIOCGRAB, 1);
	return fd;
}

int isInRect(int x, int y, int top, int bottom, int left, int right) {
	return (x > left && x < right && y > top && y < bottom);
}

int injectKey(int ufd, int key) {
	insertEvent(ufd, EV_KEY, key, 1);
	insertEvent(ufd, EV_SYN, SYN_REPORT, 0);
	insertEvent(ufd, EV_KEY, key, 0);
	insertEvent(ufd, EV_SYN, SYN_REPORT, 0);
	return 0;
}

static int wasTouched, oldX, oldY, nEventsInSwipe, ignoreTouch = 0;
static int64_t startT, lastSingleTapT;
static int lastSingleTapX, lastSingleTapY, lastSingleTapDuration;

static void decide(int ufd, int touched, int x, int y) {
	uint64_t d = now() - lastKbdTimestamp;

	if(wasTouched && !touched) {
		if (ignoreTouch) {
			ignoreTouch = 0;
		} else {
			int64_t duration = now() - startT;
			int64_t timeSinceLastSingleTap = now() - lastSingleTapT;

			if (nEventsInSwipe > 0) {
				printf("resetting touch input\n");
				// somebody please tell me what this even does
				insertEvent(ufd, EV_ABS, ABS_MT_TRACKING_ID, -1);
				insertEvent(ufd, EV_SYN, SYN_REPORT, 0);

				insertEvent(ufd, EV_ABS, ABS_MT_SLOT, 1);
				insertEvent(ufd, EV_ABS, ABS_MT_TRACKING_ID, -1);

				insertEvent(ufd, EV_SYN, SYN_REPORT, 0);
			} else {
				printf("single tap %d, %d, %d, %d; %lld %lld\n", x, y, y - oldY, x - oldX, duration, timeSinceLastSingleTap);

				if(duration < 120*1000LL && timeSinceLastSingleTap < 500*1000LL && d > 1000*1000LL) {
					printf("Got double tap\n");
					if(isInRect(oldX, oldY, 330, 361, 300, 400)) {
						printf("Double tap on space key\n");
						injectKey(ufd, KEY_TAB);
					}
				}

				lastSingleTapX = oldX;
				lastSingleTapY = oldY;
				lastSingleTapT = startT;
				lastSingleTapDuration = duration;

			}
		}
	}

	if(!touched) {
		wasTouched = 0;
		return;
	}

	if(!wasTouched && touched) {
		oldX = x;
		oldY = y;
		startT = now();
		wasTouched = touched;
		nEventsInSwipe = 0;
		return;
	}

	//800ms after typing ignore
	if(d < 800*1000) {
		oldX = x;
		oldY = y;
		ignoreTouch = 1;
	}

	if (ignoreTouch) {
		return;
	}

	nEventsInSwipe++;
	printf("%d, %d, %d, %d, %d\n", touched, x, y, y - oldY, x - oldX);

	if(x > 10 && x < (720 - 10)) {
		// map position on keyboard to position on screen
		// 10px input edge buffer on X because of the navbar swipe zone
		// 120px output buffer on X and Y to make sure we are somewhere content-y
		int xTarget = map(x, 0+10, 720-10, 0+120, 720-120);
		int yTarget = map(y, 0, 360, 0+120, 720-120);

		// more nonsense I don't understand, don't fuck with or it all breaks
		insertEvent(ufd, EV_ABS, ABS_MT_SLOT, 1);
		insertEvent(ufd, EV_ABS, ABS_MT_TRACKING_ID, 1);
		insertEvent(ufd, EV_ABS, ABS_MT_POSITION_X, xTarget);
		insertEvent(ufd, EV_ABS, ABS_MT_POSITION_Y, yTarget);

		insertEvent(ufd, EV_SYN, SYN_REPORT, 0);
	} else {
		if( (y - oldY) > 140) {
			system("cmd statusbar expand-notifications");
			oldY = y;
			oldX = x;
			return;
		}
		if( (y - oldY) < -140) {
			system("cmd statusbar collapse");
			oldY = y;
			oldX = x;
			return;
		}
	}
}

void *keyboard_monitor(void* ptr) {
	(void) ptr;
	int fd = open_ev("aw9523-key");

	while(1) {
		struct input_event e;
		if(read(fd, &e, sizeof(e)) != sizeof(e)) break;
		lastKbdTimestamp = now();
	}
	return NULL;
}

int main() {
	int ufd = uinput_init();
	int origfd = original_input_init();

	pthread_t keyboard_monitor_thread;
	pthread_create(&keyboard_monitor_thread, NULL, keyboard_monitor, NULL);

	int currentlyTouched = 0;
	int currentX = -1;
	int currentY = -1;
	while(1) {
		struct input_event e;
		if(read(origfd, &e, sizeof(e)) != sizeof(e)) break;

		if(e.type == EV_KEY && e.code == BTN_TOUCH) {
			currentlyTouched = e.value;
		}
		if(e.type == EV_ABS && e.code == ABS_MT_POSITION_X) {
			currentX = e.value;
		}
		if(e.type == EV_ABS && e.code == ABS_MT_POSITION_Y) {
			currentY = e.value;
		}
		if(e.type == EV_SYN && e.code == SYN_REPORT) {
			decide(ufd, currentlyTouched, currentX, currentY);
		}
	}
}
