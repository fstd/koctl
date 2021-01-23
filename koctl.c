// Korad KA3005P
// cc -g -Wall -Wextra -Wpedantic -o koctl koctl.c
// ./koctl -h

#define DEF_DEV "/dev/ttyACM0"


#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>
#include <err.h>
#include <getopt.h>
#include <inttypes.h>


// -1 means don't touch
static int s_mv = -1;
static int s_ma = -1;
static int s_ocp = -1;
static int s_ovp = -1;
static int s_on = -1;
static int s_lock = -1;

static bool s_log;
static bool s_qry;
static FILE *s_dev;
static char *s_tty;
static bool s_kill;

static volatile bool s_sigd = false;

static int setup(const char *device);
static void usage(FILE *str, const char *a0, int ec);
static void process_args(int argc, char **argv);
static char *getln(FILE *f, char *buf, size_t bufsz);
static char *formatnum(char *buf, size_t bufsz, int val);
static int64_t now(void);
static void qry(FILE *dev);

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
	      "\t-x: Try to disable output upon termination (use with -l)\n"
	      "\t-h: Display brief usage statement and terminate\n"
	      "\n"
	      "When no options are given, -q is implied\n"
	      "\n"
	      "The output looks like this:\n"
	      "16094888334\tON\t12.72V\t(15.12V)\t2.504A\t(2.500A)\t31.85 W\t[CC] ...\n"
	      "that is:\n"
	      "16094888334(tab)ON(tab)12.72V(tab)(15.12V)(tab)2.504A(tab)(2.500A)(tab)31.85 W(tab)[CC] ...\n"
	      "where the first column is a unix timestamp in 10ths\n"
	      "of a second.  Values within parentheses are what's set,\n"
	      "the others what's being delivered.\n"
	      "\n"
	      "(C) 2021, Timo Buhrmester (contact: fstd+koctl@pr0.tips)\n", a0);
	exit(ec);
}

static void
process_args(int argc, char **argv)
{
	char *a0 = argv[0];
	bool qry = true;
	s_tty = strdup(DEF_DEV);

	for(int ch; (ch = getopt(argc, argv, "hd:u:i:U:I:o:lqL:x")) != -1;) {
		switch (ch) {
		case 'd':
			free(s_tty);
			s_tty = strdup(optarg);
			break;
		case 'u':
			qry = false;
			s_mv = (int)(strtod(optarg, NULL) * 1000);
			if (s_mv < 0 || s_mv > 30000)
				errx(EXIT_FAILURE, "out of range: %d mV", s_mv);
			break;
		case 'i':
			qry = false;
			s_ma = (int)(strtod(optarg, NULL) * 1000);
			if (s_ma < 0 || s_ma > 5000)
				errx(EXIT_FAILURE, "out of range: %d mA", s_ma);
			break;
		case 'U':
			qry = false;
			s_ovp = strtoul(optarg, NULL, 10);
			break;
		case 'I':
			qry = false;
			s_ocp = strtoul(optarg, NULL, 10);
			break;
		case 'L':
			qry = false;
			s_lock = strtoul(optarg, NULL, 10);
			break;
		case 'o':
			qry = false;
			s_on = strtoul(optarg, NULL, 10);
			break;
		case 'l':
			qry = false;
			s_log = true;
			break;
		case 'x':
			s_kill = true;
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

	if (!s_qry)
		s_qry = qry;
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

static void
qry(FILE *dev)
{
	char tmpstr[16];
	char auset[16], *puset = auset;
	char auout[16], *puout = auout;
	char aiset[16], *piset = aiset;
	char aiout[16], *piout = aiout;

	int64_t tnow = now();
	fprintf(dev, "STATUS?\rVOUT1?\rIOUT1?\rVSET1?\rISET1?\r");
	getln(dev, tmpstr, sizeof tmpstr);
	getln(dev, auout, sizeof auout);
	getln(dev, aiout, sizeof aiout);
	getln(dev, auset, sizeof auset);
	getln(dev, aiset, sizeof aiset);

	while (puout[0] == '0' && isdigit(puout[1])) puout++;
	while (puset[0] == '0' && isdigit(puset[1])) puset++;
	while (piout[0] == '0' && isdigit(piout[1])) piout++;
	while (piset[0] == '0' && isdigit(piset[1])) piset++;

	uint8_t c = tmpstr[0];
	bool on = c & 0x40u;

	char cvcc[6] = "";
	if (on)
	    snprintf(cvcc, sizeof cvcc, "[%s] ", c & 0x01u ? "CV" : "CC");

	char statstr[64];
	snprintf(statstr, sizeof statstr, "%s%s%s%s",
	    cvcc,
	    c & 0x10u ? "" : "[beep off] ",
	    c & 0x20u ? "[OCP on] " : "",
	    c & 0x80u ? "[OVP on] " : "");

	printf("%"PRIi64"\t%s\t%sV (%sV)\t%sA (%sA)\t%.2f W\t%s\n",
	    tnow/100000,
	    (uint8_t)c & 0x40 ? "ON" : "off",
	    puout, puset, piout, piset,
	    strtod(auout, NULL) * strtod(aiout, NULL),
	    statstr);
}

void
exithook(void)
{
	if (s_kill) {
		fprintf(s_dev, "OUT0\r");
	}
	fclose(s_dev);
}

void
sighnd(int sig)
{
	s_sigd = true;
}

int
main(int argc, char **argv)
{
	process_args(argc, argv);

	atexit(exithook);
	signal(SIGINT, sighnd);
	signal(SIGHUP, sighnd);
	signal(SIGTERM, sighnd);

	s_dev = fdopen(setup(s_tty), "w+");
	if (!s_dev)
		err(EXIT_FAILURE, "fdopen");

	char tmpstr[16];

	if (s_on == 0)
		fprintf(s_dev, "OUT0\r");

	if (s_mv >= 0) {
		formatnum(tmpstr, sizeof tmpstr, s_mv);
		fprintf(s_dev, "VSET1:%s\r", tmpstr);
	}

	if (s_ma >= 0) {
		formatnum(tmpstr, sizeof tmpstr, s_ma);
		fprintf(s_dev, "ISET1:%s\r", tmpstr);
	}

	if (s_ocp != -1)
		fprintf(s_dev, "OCP%d\r", !!s_ocp);

	if (s_ovp != -1)
		fprintf(s_dev, "OVP%d\r", !!s_ovp);

	if (s_lock != -1)
		fprintf(s_dev, "LOCK%d\r", !!s_lock);

	if (s_on >= 1)
		fprintf(s_dev, "OUT1\r");

	if (s_log) {
		int64_t tnext = now();
		while (!s_sigd) {
			int64_t tnow = now();
			if (tnow >= tnext) {
				qry(s_dev);
				tnext += 100000;
			}

			int64_t twait = tnext - now();
			if (twait > 0)
				usleep(twait);
		}
	} else if (s_qry)
		qry(s_dev);

	exit(0);
}
