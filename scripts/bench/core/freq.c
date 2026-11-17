/*
 * scripts/bench/core/freq.c: the frequency reader of the bench core.
 *
 * Reads the state residency IOReport keeps for each CPU cluster (the
 * "CPU Stats" group, "CPU Complex Performance States" subgroup) and for
 * the GPU (the "GPU Stats" group, "GPU Performance States" subgroup,
 * channel GPUPH), and maps each active state onto the frequency table the
 * power manager publishes in the I/O registry (the "voltage-states*"
 * properties of IODeviceTree pmgr: pairs of a frequency and a voltage).
 * No root and no powermetrics: IOReport is reached through dlopen of
 * /usr/lib/libIOReport.dylib, the registry through IOKit.
 *
 *   freq dump                    cumulative counters, one line a domain:
 *                                dump.<domain>=<idle> <mhz>:<ticks> ...
 *   freq delta <before> <after>  the window between two dumps:
 *                                freq.<domain>.mhz=<weighted MHz, active>
 *                                freq.<domain>.top=<% of active at the top>
 *                                freq.<domain>.active=<% of the window>
 *                                freq.<domain>.max=<the top state's MHz>
 *   freq <seconds>               dump, sleep, dump, delta
 *   freq list                    the channels and tables, for a person
 *
 * A domain is the channel's name in lower case (ecpu, pcpu, ..., gpu).
 * What cannot be read prints "freq=unavailable reason=<word>" and exits 0.
 * Built by scripts/bench/core/snap.sh with the host's cc; never part of
 * the engine. See docs/plans/bench-harness.md ("Determinism").
 */

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_DOMAINS 16
#define MAX_STATES 64
#define MAX_TABLES 64

typedef struct IOReportSubscriptionCF *IOReportSubscriptionRef;

static CFDictionaryRef (*ior_copy_group)(CFStringRef, CFStringRef, uint64_t, uint64_t, uint64_t);
static void (*ior_merge)(CFDictionaryRef, CFDictionaryRef, CFTypeRef);
static IOReportSubscriptionRef (*ior_subscribe)(void *, CFMutableDictionaryRef, CFMutableDictionaryRef *, uint64_t, CFTypeRef);
static CFDictionaryRef (*ior_samples)(IOReportSubscriptionRef, CFMutableDictionaryRef, CFTypeRef);
static CFStringRef (*ior_group)(CFDictionaryRef);
static CFStringRef (*ior_subgroup)(CFDictionaryRef);
static CFStringRef (*ior_name)(CFDictionaryRef);
static int32_t (*ior_state_count)(CFDictionaryRef);
static CFStringRef (*ior_state_name)(CFDictionaryRef, int32_t);
static int64_t (*ior_residency)(CFDictionaryRef, int32_t);

typedef struct
{
	char name[32];
	int nstates;
	char state[MAX_STATES][16];
	long long ticks[MAX_STATES];
	double mhz[MAX_STATES]; /* 0 an idle state, -1 one past the table */
	int fit;
} domain_t;

typedef struct
{
	char key[48];
	int n;
	double mhz[MAX_STATES];
} table_t;

static domain_t domains[MAX_DOMAINS];
static int ndomains;
static table_t tables[MAX_TABLES];
static int ntables;

static void unavailable(const char *reason) __attribute__((noreturn));

static void
unavailable(const char *reason)
{
	printf("freq=unavailable reason=%s\n", reason);
	exit(0);
}

static void
cfstr(CFStringRef s, char *buf, size_t len)
{
	buf[0] = '\0';
	if (s == NULL || !CFStringGetCString(s, buf, (CFIndex)len, kCFStringEncodingUTF8))
	{
		buf[0] = '\0';
	}
}

static int
idle_state(const char *s)
{
	return strcmp(s, "IDLE") == 0 || strcmp(s, "OFF") == 0 || strcmp(s, "DOWN") == 0;
}

static void
load_ioreport(void)
{
	void *h = dlopen("/usr/lib/libIOReport.dylib", RTLD_NOW);

	if (h == NULL)
	{
		unavailable("no-ioreport");
	}
	ior_copy_group = dlsym(h, "IOReportCopyChannelsInGroup");
	ior_merge = dlsym(h, "IOReportMergeChannels");
	ior_subscribe = dlsym(h, "IOReportCreateSubscription");
	ior_samples = dlsym(h, "IOReportCreateSamples");
	ior_group = dlsym(h, "IOReportChannelGetGroup");
	ior_subgroup = dlsym(h, "IOReportChannelGetSubGroup");
	ior_name = dlsym(h, "IOReportChannelGetChannelName");
	ior_state_count = dlsym(h, "IOReportStateGetCount");
	ior_state_name = dlsym(h, "IOReportStateGetNameForIndex");
	ior_residency = dlsym(h, "IOReportStateGetResidency");
	if (!ior_copy_group || !ior_merge || !ior_subscribe || !ior_samples || !ior_group ||
		!ior_subgroup || !ior_name || !ior_state_count || !ior_state_name || !ior_residency)
	{
		unavailable("ioreport-symbols");
	}
}

/* The frequency tables of the power manager: every voltage-states<n>
 * and voltage-states<n>-sram property of the AppleARMIODevice nodes named
 * pmgr or pmgr-<something> (the node that holds them differs between
 * chips), pairs of 32-bit words (a frequency, a voltage). A table in Hz is
 * told from one in kHz by its size: no core or GPU runs under 10 MHz or
 * over 10 GHz, so a frequency over 1e7 is in Hz. A zero frequency (the off
 * state some tables start with) is dropped; a table with a frequency out
 * of that range is not a frequency table and is dropped whole. */
static void
load_node_tables(io_registry_entry_t e)
{
	CFMutableDictionaryRef props = NULL;
	CFIndex n, i;
	const void **keys, **vals;

	if (IORegistryEntryCreateCFProperties(e, &props, kCFAllocatorDefault, 0) != KERN_SUCCESS || props == NULL)
	{
		return;
	}
	n = CFDictionaryGetCount(props);
	keys = calloc((size_t)n + 1, sizeof(*keys));
	vals = calloc((size_t)n + 1, sizeof(*vals));
	if (keys == NULL || vals == NULL)
	{
		exit(1);
	}
	CFDictionaryGetKeysAndValues(props, keys, vals);
	for (i = 0; i < n && ntables < MAX_TABLES; i++)
	{
		char k[48];
		const UInt8 *b;
		CFIndex len, j;
		table_t *t;

		if (CFGetTypeID(keys[i]) != CFStringGetTypeID() || CFGetTypeID(vals[i]) != CFDataGetTypeID())
		{
			continue;
		}
		cfstr(keys[i], k, sizeof(k));
		if (strncmp(k, "voltage-states", 14) != 0)
		{
			continue;
		}
		b = CFDataGetBytePtr(vals[i]);
		len = CFDataGetLength(vals[i]);
		t = &tables[ntables];
		memset(t, 0, sizeof(*t));
		snprintf(t->key, sizeof(t->key), "%s", k);
		for (j = 0; j + 8 <= len && t->n < MAX_STATES; j += 8)
		{
			uint32_t f = (uint32_t)b[j] | (uint32_t)b[j + 1] << 8 | (uint32_t)b[j + 2] << 16 | (uint32_t)b[j + 3] << 24;
			double mhz = f > 10000000u ? f / 1e6 : f / 1e3;

			if (f == 0)
			{
				continue;
			}
			if (mhz < 10 || mhz > 10000)
			{
				t->n = 0;
				break;
			}
			t->mhz[t->n++] = mhz;
		}
		if (t->n > 0)
		{
			ntables++;
		}
	}
	free(keys);
	free(vals);
	CFRelease(props);
}

static void
load_tables(void)
{
	io_iterator_t it;
	io_registry_entry_t x;

	if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("AppleARMIODevice"), &it) != KERN_SUCCESS)
	{
		return;
	}
	while ((x = IOIteratorNext(it)) != MACH_PORT_NULL)
	{
		io_name_t nm;

		if (IORegistryEntryGetName(x, nm) == KERN_SUCCESS &&
			(strcmp(nm, "pmgr") == 0 || strncmp(nm, "pmgr-", 5) == 0))
		{
			load_node_tables(x);
		}
		IOObjectRelease(x);
	}
	IOObjectRelease(it);
}

static void
sample(void)
{
	CFDictionaryRef cpu, gpu, s;
	CFMutableDictionaryRef chans, subbed = NULL;
	IOReportSubscriptionRef sub;
	CFArrayRef arr;
	CFIndex i, n;

	cpu = ior_copy_group(CFSTR("CPU Stats"), NULL, 0, 0, 0);
	gpu = ior_copy_group(CFSTR("GPU Stats"), NULL, 0, 0, 0);
	if (cpu == NULL && gpu == NULL)
	{
		unavailable("no-channels");
	}
	chans = CFDictionaryCreateMutableCopy(kCFAllocatorDefault, 0, cpu ? cpu : gpu);
	if (cpu != NULL && gpu != NULL)
	{
		ior_merge(chans, gpu, NULL);
	}
	sub = ior_subscribe(NULL, chans, &subbed, 0, NULL);
	if (sub == NULL || subbed == NULL)
	{
		unavailable("no-subscription");
	}
	s = ior_samples(sub, subbed, NULL);
	if (s == NULL)
	{
		unavailable("no-samples");
	}
	arr = CFDictionaryGetValue(s, CFSTR("IOReportChannels"));
	if (arr == NULL || CFGetTypeID(arr) != CFArrayGetTypeID())
	{
		unavailable("no-samples");
	}
	n = CFArrayGetCount(arr);
	for (i = 0; i < n && ndomains < MAX_DOMAINS; i++)
	{
		CFDictionaryRef ch = CFArrayGetValueAtIndex(arr, i);
		char g[64], sg[64], nm[32];
		domain_t *d;
		int32_t k, c;

		cfstr(ior_group(ch), g, sizeof(g));
		cfstr(ior_subgroup(ch), sg, sizeof(sg));
		cfstr(ior_name(ch), nm, sizeof(nm));
		if (strchr(nm, '_') != NULL ||
			!((strcmp(g, "CPU Stats") == 0 && strcmp(sg, "CPU Complex Performance States") == 0) ||
				(strcmp(g, "GPU Stats") == 0 && strcmp(sg, "GPU Performance States") == 0 && strcmp(nm, "GPUPH") == 0)))
		{
			continue;
		}
		c = ior_state_count(ch);
		if (c <= 0 || c > MAX_STATES)
		{
			continue;
		}
		d = &domains[ndomains++];
		memset(d, 0, sizeof(*d));
		if (strcmp(nm, "GPUPH") == 0)
		{
			snprintf(d->name, sizeof(d->name), "gpu");
		}
		else
		{
			for (k = 0; nm[k] && k < (int32_t)sizeof(d->name) - 1; k++)
			{
				char x = nm[k];

				d->name[k] = (x >= 'A' && x <= 'Z') ? (char)(x - 'A' + 'a') : x;
				if (!((d->name[k] >= 'a' && d->name[k] <= 'z') || (d->name[k] >= '0' && d->name[k] <= '9')))
				{
					d->name[k] = '_';
				}
			}
		}
		d->nstates = c;
		for (k = 0; k < c; k++)
		{
			cfstr(ior_state_name(ch, k), d->state[k], sizeof(d->state[k]));
			d->ticks[k] = ior_residency(ch, k);
		}
	}
	if (ndomains == 0)
	{
		unavailable("no-pstate-channels");
	}
}

/* Picks the table for each domain. A table fits when its frequencies
 * rise (a table that falls holds something else) and it has as many as
 * the domain has active states, or fewer but at least as many as the
 * states up to the last one that was ever used: the GPU lists states past
 * its table that it never enters, and those are left out. The best fit
 * has the most frequencies; of equals, a CPU cluster takes a table whose
 * name ends in "-sram" and the GPU one whose name does not. A domain no
 * table fits is left out of the dump. Returns how many fit. */
static int
assign_tables(int say)
{
	int i, fit = 0;

	for (i = 0; i < ndomains; i++)
	{
		domain_t *d = &domains[i];
		int active = 0, used = 0, k, t, pick = -1, best = -1;
		int gpu = strcmp(d->name, "gpu") == 0;

		for (k = 0; k < d->nstates; k++)
		{
			if (!idle_state(d->state[k]))
			{
				active++;
				if (d->ticks[k] != 0)
				{
					used = active;
				}
			}
		}
		for (t = 0; t < ntables; t++)
		{
			int sram = strstr(tables[t].key, "-sram") != NULL, rise = 1, j, score;

			for (j = 1; j < tables[t].n; j++)
			{
				if (tables[t].mhz[j] < tables[t].mhz[j - 1])
				{
					rise = 0;
				}
			}
			if (!rise || tables[t].n > active || tables[t].n < used || tables[t].n == 0)
			{
				continue;
			}
			score = 2 * tables[t].n + (sram != gpu);
			if (score > best)
			{
				best = score;
				pick = t;
			}
		}
		d->fit = pick >= 0;
		if (pick < 0)
		{
			if (say)
			{
				printf("domain %s: no table fits its %d active states\n", d->name, active);
			}
			continue;
		}
		if (say)
		{
			printf("domain %s: table %s\n", d->name, tables[pick].key);
		}
		fit++;
		for (k = 0, t = 0; k < d->nstates; k++)
		{
			if (idle_state(d->state[k]))
			{
				d->mhz[k] = 0;
			}
			else
			{
				d->mhz[k] = t < tables[pick].n ? tables[pick].mhz[t] : -1;
				t++;
			}
		}
	}
	return fit;
}

static void
dump(FILE *out)
{
	int i, k;

	for (i = 0; i < ndomains; i++)
	{
		long long idle = 0;

		if (!domains[i].fit)
		{
			continue;
		}
		for (k = 0; k < domains[i].nstates; k++)
		{
			if (domains[i].mhz[k] == 0)
			{
				idle += domains[i].ticks[k];
			}
		}
		fprintf(out, "dump.%s=%lld", domains[i].name, idle);
		for (k = 0; k < domains[i].nstates; k++)
		{
			if (domains[i].mhz[k] > 0)
			{
				fprintf(out, " %.0f:%lld", domains[i].mhz[k], domains[i].ticks[k]);
			}
		}
		fprintf(out, "\n");
	}
}

/* A parsed dump line: a domain, its idle ticks and its active states. */
typedef struct
{
	char name[32];
	long long idle;
	int n;
	double mhz[MAX_STATES];
	long long ticks[MAX_STATES];
} line_t;

static int
read_dump(const char *path, line_t *out, int max)
{
	FILE *f = fopen(path, "r");
	char buf[4096];
	int n = 0;

	if (f == NULL)
	{
		return -1;
	}
	while (n < max && fgets(buf, sizeof(buf), f) != NULL)
	{
		char *eq, *p, *end;
		line_t *l = &out[n];

		if (strncmp(buf, "dump.", 5) != 0 || (eq = strchr(buf, '=')) == NULL || eq - buf - 5 >= (long)sizeof(l->name) || eq - buf - 5 < 1)
		{
			continue;
		}
		memset(l, 0, sizeof(*l));
		memcpy(l->name, buf + 5, (size_t)(eq - buf - 5));
		p = eq + 1;
		l->idle = strtoll(p, &end, 10);
		if (end == p)
		{
			continue;
		}
		p = end;
		while (*p == ' ' && l->n < MAX_STATES)
		{
			double m = strtod(p + 1, &end);

			if (end == p + 1 || *end != ':')
			{
				break;
			}
			p = end + 1;
			l->mhz[l->n] = m;
			l->ticks[l->n] = strtoll(p, &end, 10);
			if (end == p)
			{
				break;
			}
			p = end;
			l->n++;
		}
		n++;
	}
	fclose(f);
	return n;
}

static int
delta(const char *a, const char *b)
{
	static line_t la[MAX_DOMAINS], lb[MAX_DOMAINS];
	int na = read_dump(a, la, MAX_DOMAINS), nb = read_dump(b, lb, MAX_DOMAINS);
	int i, j, k, said = 0;

	if (na <= 0 || nb <= 0)
	{
		unavailable("no-dump");
	}
	for (i = 0; i < nb; i++)
	{
		line_t *y = &lb[i], *x = NULL;
		double wsum = 0, act = 0, top = 0, max = 0, idle;

		for (j = 0; j < na; j++)
		{
			if (strcmp(la[j].name, y->name) == 0)
			{
				x = &la[j];
			}
		}
		if (x == NULL || x->n != y->n)
		{
			continue;
		}
		idle = (double)(y->idle - x->idle);
		for (k = 0; k < y->n; k++)
		{
			double r = (double)(y->ticks[k] - x->ticks[k]);

			if (r < 0 || y->mhz[k] != x->mhz[k])
			{
				unavailable("counters-went-back");
			}
			wsum += r * y->mhz[k];
			act += r;
			if (y->mhz[k] >= max)
			{
				max = y->mhz[k];
				top = r;
			}
		}
		if (idle < 0)
		{
			unavailable("counters-went-back");
		}
		printf("freq.%s.mhz=%.0f\n", y->name, act > 0 ? wsum / act : 0);
		printf("freq.%s.top=%.1f\n", y->name, act > 0 ? 100 * top / act : 0);
		printf("freq.%s.active=%.1f\n", y->name, act + idle > 0 ? 100 * act / (act + idle) : 0);
		printf("freq.%s.max=%.0f\n", y->name, max);
		said++;
	}
	if (said == 0)
	{
		unavailable("no-common-domain");
	}
	return 0;
}

static void
read_all(void)
{
	load_ioreport();
	load_tables();
	if (ntables == 0)
	{
		unavailable("no-frequency-table");
	}
	ndomains = 0;
	sample();
	if (assign_tables(0) == 0)
	{
		unavailable("no-table-fits");
	}
}

/* dump_to <template>: a dump into a new temporary file named from
 * <template> (mkstemp). */
static void
dump_to(char *path)
{
	int fd = mkstemp(path);
	FILE *out;

	if (fd < 0 || (out = fdopen(fd, "w")) == NULL)
	{
		unavailable("no-temporary-file");
	}
	read_all();
	dump(out);
	if (fclose(out) != 0)
	{
		unavailable("no-temporary-file");
	}
}

int
main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "dump") == 0)
	{
		read_all();
		dump(stdout);
		return 0;
	}
	if (argc == 4 && strcmp(argv[1], "delta") == 0)
	{
		return delta(argv[2], argv[3]);
	}
	if (argc == 2 && strcmp(argv[1], "list") == 0)
	{
		int i, k;

		load_ioreport();
		load_tables();
		sample();
		for (i = 0; i < ntables; i++)
		{
			printf("table %s n=%d:", tables[i].key, tables[i].n);
			for (k = 0; k < tables[i].n; k++)
			{
				printf(" %.0f", tables[i].mhz[k]);
			}
			printf("\n");
		}
		for (i = 0; i < ndomains; i++)
		{
			printf("domain %s states=%d:", domains[i].name, domains[i].nstates);
			for (k = 0; k < domains[i].nstates; k++)
			{
				printf(" %s=%lld", domains[i].state[k], domains[i].ticks[k]);
			}
			printf("\n");
		}
		assign_tables(1);
		dump(stdout);
		return 0;
	}
	if (argc == 2 && argv[1][0] >= '1' && argv[1][0] <= '9' && strlen(argv[1]) <= 5 &&
		strspn(argv[1], "0123456789") == strlen(argv[1]))
	{
		const char *td = getenv("TMPDIR");
		char a[1024], b[1024];
		int st;

		if (td == NULL || *td == '\0')
		{
			td = "/tmp";
		}
		snprintf(a, sizeof(a), "%s/bench-freq.XXXXXX", td);
		snprintf(b, sizeof(b), "%s/bench-freq.XXXXXX", td);
		dump_to(a);
		sleep((unsigned)atoi(argv[1]));
		dump_to(b);
		st = delta(a, b);
		unlink(a);
		unlink(b);
		return st;
	}
	fprintf(stderr, "usage: freq dump | freq delta <before> <after> | freq <seconds> | freq list\n");
	return 2;
}
