// Korad KA3005P
// cc -g -Wall -Wextra -Wpedantic -o koctl koctl.c
// ./koctl -h

#define DEF_DEV "/dev/ttyACM0"


#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>
#include <err.h>
#include <getopt.h>
#include <inttypes.h>


#define MODE_QRY 0
#define MODE_SET 1
#define MODE_LOG 2

static int s_mode = MODE_QRY;

// -1 means don't touch
static int s_mv = -1;
static int s_ma = -1;
static int s_ocp = -1;
static int s_ovp = -1;
static int s_on = -1;
static int s_lock = -1;

static bool s_qry;
static char *s_dev;

static int setup(const char *device);
static void usage(FILE *str, const char *a0, int ec);
static void process_args(int argc, char **argv);
static char *getln(FILE *f, char *buf, size_t bufsz);
static char *formatnum(char *buf, size_t bufsz, int val);
static int64_t now(void);

int main(int argc, char **argv);

static int
setup(const char *device)
{
	struct termios config;
	int fd = open(device, O_RDWR | O_NOCTTY);
	if(fd == -1)
		err(EXIT_FAILURE, "open(%s)", device);

	if(!isatty(fd))
		err(EXIT_FAILURE, "isatty");

	if(tcgetattr(fd, &config) < 0)
		err(EXIT_FAILURE, "tcgetattr");

	config.c_iflag &= ~(IGNBRK | BRKINT | ICRNL
	    | INLCR | PARMRK | INPCK | ISTRIP | IXON);

	config.c_oflag = 0;
	config.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN | ISIG);
	config.c_cflag &= ~(CSIZE | PARENB);
	config.c_cflag |= CS8;
	config.c_cc[VMIN] = 1;
	config.c_cc[VTIME] = 0;

	if(cfsetispeed(&config, B9600) < 0 || cfsetospeed(&config, B9600) < 0)
		err(EXIT_FAILURE, "cfsetispeed/cfsetospeed");

	if(tcsetattr(fd, TCSAFLUSH, &config) < 0)
		err(EXIT_FAILURE, "tcsetattr");

	return fd;
}

static void
usage(FILE *str, const char *a0, int ec)
{
	fprintf(str,
	      "===========\n"
	      "== koctl ==\n"
	      "===========\n"
	      "usage: %s [-d <dev>] [-u <volts>] [-i <amps>] [-{UIoL} 1|0] [-lqh]\n"
	      "\t-d <device>: UART device (def: " DEF_DEV ")\n"
	      "\t-u <voltage>: Set target voltage (0.0 - 30.0)\n"
	      "\t-i <current>: Set target current (0.0 - 5.0)\n"
	      "\t-U <1|0>: Enable/disable over-voltage protection\n"
	      "\t-I <1|0>: Enable/disable over-current protection\n"
	      "\t-o <1|0>: Enable/disable output\n"
	      "\t-L <1|0>: Enable/disable front panel lock\n"
	      "\t-l: Log U/I/P to stdout\n"
	      "\t-q: Force one U/I/P output\n"
	      "\t-h: Display brief usage statement and terminate\n"
	      "\n"
	      "When no options are given, -q is implied\n"
	      "\n"
	      "The output looks like this:\n"
	      "16094778119\t20.32 V\t4.001 A\t81.30 W\n"
	      "that is:\n"
	      "16094778119(tab)20.32 V(tab)4.001 A(tab)81.30 W\n"
	      "where the first column is a unix timestamp in 10ths\n"
	      "of a second\n"
	      "\n"
	      "(C) 2021, Timo Buhrmester (contact: fstd+koctl@pr0.tips)\n", a0);
	exit(ec);
}

static void
process_args(int argc, char **argv)
{
	char *a0 = argv[0];
	s_mode = MODE_QRY;
	s_dev = strdup(DEF_DEV);

	for(int ch; (ch = getopt(argc, argv, "hd:u:i:U:I:o:lqL:")) != -1;) {
		switch (ch) {
		case 'd':
			free(s_dev);
			s_dev = strdup(optarg);
			break;
		case 'u':
			s_mode = MODE_SET;
			s_mv = (int)(strtod(optarg, NULL) * 1000);
			if (s_mv < 0 || s_mv > 30000)
				errx(EXIT_FAILURE, "out of range: %d mV", s_mv);
			break;
		case 'i':
			s_mode = MODE_SET;
			s_ma = (int)(strtod(optarg, NULL) * 1000);
			if (s_ma < 0 || s_ma > 5000)
				errx(EXIT_FAILURE, "out of range: %d mA", s_ma);
			break;
		case 'U':
			s_mode = MODE_SET;
			s_ovp = strtoul(optarg, NULL, 10);
			break;
		case 'I':
			s_mode = MODE_SET;
			s_ocp = strtoul(optarg, NULL, 10);
			break;
		case 'L':
			s_mode = MODE_SET;
			s_lock = strtoul(optarg, NULL, 10);
			break;
		case 'o':
			s_mode = MODE_SET;
			s_on = strtoul(optarg, NULL, 10);
			break;
		case 'l':
			s_mode = MODE_LOG;
			break;
		case 'q':
			s_qry = true;
			break;
		case 'h':
			usage(stdout, a0, EXIT_SUCCESS);
			break;
		default:
			usage(stderr, a0, EXIT_FAILURE);
		}
	}
}


static char *
getln(FILE *f, char *buf, size_t bufsz)
{
	if (!fgets(buf, bufsz, f))
		err(EXIT_FAILURE, "fgets");

	size_t len = strlen(buf);

	while (len > 0 && buf[len-1] == '\n')
		buf[--len] = '\0';
	return buf;
}

static char *
formatnum(char *buf, size_t bufsz, int val)
{
	int w = val / 1000;
	const char *fill = "";

	val %= 1000;

	if (val <= 9)
		fill = "00";
	else if (val <= 99)
		fill = "0";

	snprintf(buf, bufsz, "%d.%s%d", w, fill, val);
	return buf;
}

static int64_t
now(void)
{
	struct timeval t;
	if (gettimeofday(&t, NULL) != 0)
		err(EXIT_FAILURE, "gettimeofday");

	return t.tv_sec * 1000000ll + t.tv_usec;
}

int
main(int argc, char **argv)
{
	process_args(argc, argv);

	FILE *dev = fdopen(setup(s_dev), "w+");
	if (!dev)
		err(EXIT_FAILURE, "fdopen");

	char ustr[16];
	char istr[16];

	if (s_mode == MODE_LOG) {
		int64_t tnext = now();
		for (;;) {
			int64_t tnow = now();
			if (tnow >= tnext) {
				fprintf(dev, "VOUT1?\rIOUT1?\r");
				getln(dev, ustr, sizeof ustr);
				getln(dev, istr, sizeof istr);
				printf("%"PRIi64"\t%s V\t%s A\t%.2f W\n",
				    tnow/100000, ustr, istr,
				    strtod(ustr, NULL) * strtod(istr, NULL));
				tnext += 100000;
			}

			int64_t twait = tnext - now();
			if (twait > 0)
				usleep(twait);
		}
	}

	if (s_mode == MODE_SET) {
		if (s_on == 0)
			fprintf(dev, "OUT0\r");

		if (s_mv >= 0) {
			formatnum(ustr, sizeof ustr, s_mv);
			fprintf(dev, "VSET1:%s\r", ustr);
		}

		if (s_ma >= 0) {
			formatnum(istr, sizeof istr, s_ma);
			fprintf(dev, "ISET1:%s\r", istr);
		}

		if (s_ocp != -1)
			fprintf(dev, "OCP%d\r", !!s_ocp);

		if (s_ovp != -1)
			fprintf(dev, "OVP%d\r", !!s_ovp);

		if (s_lock != -1)
			fprintf(dev, "LOCK%d\r", !!s_lock);

		if (s_on >= 1)
			fprintf(dev, "OUT1\r");
	}

	if (s_mode == MODE_QRY || s_qry) {
		int64_t tnow = now();
		fprintf(dev, "VOUT1?\rIOUT1?\r");
		getln(dev, ustr, sizeof ustr);
		getln(dev, istr, sizeof istr);
		printf("%"PRIi64"\t%s V\t%s A\t%.3f W\n",
		    tnow/100000, ustr, istr,
		    strtod(ustr, NULL) * strtod(istr, NULL));
	}

	fclose(dev);

	return 0;
}
