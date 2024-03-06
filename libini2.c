#include "libini2.h"

#include "libini/ini.h"

#include <errno.h>
#include <iio.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>

/*#ifdef _WIN32
#define LONG_LONG_FORMAT "%I64d"
#else*/
#define LONG_LONG_FORMAT "%lld"
//#endif

struct load_store_params {
	const struct iio_device *dev;
	const char * const *whitelist;
	size_t list_len;
	bool is_debug;
	FILE *f;
	struct INI *ini;
	const void *section_top;
};

static bool attr_matches(const char *dev_name, size_t dev_len,
		const char *attr, size_t attr_len,
		const char *key, size_t len, bool debug)
{
	return (!debug && (len == dev_len + 1 + attr_len) &&
			!strncmp(key, dev_name, dev_len) &&
			!strncmp(key + dev_len + 1, attr, attr_len)) ||
		(debug && (len == dev_len + sizeof("debug.") + attr_len) &&
			!strncmp(key, "debug.", sizeof("debug.") - 1) &&
			!strncmp(key + sizeof("debug.") - 1, dev_name, dev_len) &&
			!strncmp(key + sizeof("debug.") + dev_len, attr, attr_len));
}

static bool attr_in_whitelist(const char *attr,
		const char *dev_name, size_t dev_len, bool is_debug,
		const char * const *whitelist, size_t list_len)
{
	unsigned int i;
	for (i = 0; i < list_len && whitelist[i]; i++)
		if ((!is_debug && !strncmp(whitelist[i], dev_name, dev_len) &&
					!strcmp(whitelist[i] + dev_len + 1, attr)) ||
				(is_debug && !strncmp(whitelist[i], "debug.", sizeof("debug.") - 1) &&
					!strncmp(whitelist[i] + sizeof("debug.") - 1, dev_name, dev_len) &&
					!strcmp(whitelist[i] + sizeof("debug.") + dev_len, attr)))
			return true;
	return false;
}

static ssize_t read_from_ini(struct load_store_params *params,
		const char *dev_name, size_t name_len,
		const char *attr, void *buf, size_t len)
{
	bool found = false;
	const char *key, *value;
	size_t klen, vlen;

	if (!len)
		return 0;

	/* Rewind to the beginning of the section */
	ini_set_read_pointer(params->ini, params->section_top);

	while (!found && ini_read_pair(params->ini,
				&key, &klen, &value, &vlen) > 0)
		found = attr_matches(dev_name, name_len, attr, strlen(attr),
				key, klen, params->is_debug);
	if (!found)
		return 0;

	if (len > vlen)
		len = vlen;
	memcpy(buf, value, len);
	return (ssize_t) len;
}

static ssize_t update_from_ini_dev_cb(struct iio_device *dev,
		const char *attr, void *buf, size_t len, void *d)
{
	struct load_store_params *params = (struct load_store_params *) d;
	const char *dev_name = iio_device_get_name(dev);
	size_t name_len = dev_name ? strlen(dev_name) : 0;

	if (attr_in_whitelist(attr, dev_name, name_len, params->is_debug,
				params->whitelist, params->list_len))
		return read_from_ini(params,
				dev_name, name_len, attr, buf, len);
	return 0;
}

static ssize_t update_from_ini_chn_cb(struct iio_channel *chn,
		const char *attr, void *buf, size_t len, void *d)
{
	struct load_store_params *params = (struct load_store_params *) d;
	const char *dev_name = iio_device_get_name(params->dev);
	size_t name_len = dev_name ? strlen(dev_name) : 0;
	bool is_hardwaregain = !strncmp(attr, "hardwaregain", len);

	attr = iio_channel_attr_get_filename(chn, attr);
	if (attr_in_whitelist(attr, dev_name, name_len, false,
				params->whitelist, params->list_len)) {
		ssize_t ret = read_from_ini(params,
				dev_name, name_len, attr, buf, len);

		/* Dirty workaround that strips the "dB" suffix of
		 * hardwaregain value. Fix me when possible. */
		if (is_hardwaregain) {
			char *tmp = strstr((char *) buf, " dB");
			if (tmp)
				*tmp = '\0';
		}
		return ret;
	}
	return 0;
}

void update_from_ini(const char *ini_file,
		const char *driver_name, struct iio_device *dev,
		const char * const *whitelist, size_t list_len)
{
	bool found = false;
	const char *name;
	size_t nlen, dlen;
	unsigned int i;
	struct INI *ini = ini_open(ini_file);
	struct load_store_params params = {
		.dev = dev,
		.whitelist = whitelist,
		.list_len = list_len,
		.is_debug = false,
		.ini = ini,
	};

	if (!ini) {
		fprintf(stderr, "ERROR: Cannot open INI file %s\n", ini_file);
		return;
	}

	dlen = strlen(driver_name);

	while (!found && ini_next_section(ini, &name, &nlen) > 0) {
		if (nlen == dlen)
			found = !strncmp(name, driver_name, nlen);
	}

	if (!found) {
		fprintf(stderr, "error parsing %s file: Could not find %s\n",
				ini_file, driver_name);
		ini_close(ini);
		return;
	}

	params.section_top = name + nlen + 1;

	for (i = 0; i < iio_device_get_channels_count(dev); i++)
		iio_channel_attr_write_all(iio_device_get_channel(dev, i),
				update_from_ini_chn_cb, &params);

	if (iio_device_get_attrs_count(dev))
		iio_device_attr_write_all(dev, update_from_ini_dev_cb, &params);

	params.is_debug = true;
	iio_device_debug_attr_write_all(dev, update_from_ini_dev_cb, &params);

	ini_close(ini);
}

char * read_token_from_ini(const char *ini_file,
		const char *driver_name, const char *token)
{
	char *dup;
	const char *name, *key, *value;
	size_t nlen, klen, vlen, tlen = strlen(token);
	bool found = false;
	struct INI *ini = ini_open(ini_file);
	if (!ini)
		return NULL;

	while (!found && ini_next_section(ini, &name, &nlen) > 0)
		found = !strncmp(name, driver_name, nlen);
	if (!found)
		return NULL;

	found = false;
	while (!found && ini_read_pair(ini, &key, &klen, &value, &vlen) > 0)
		found = (tlen == klen) && !strncmp(token, key, klen);
	if (!found)
		return NULL;

	dup = malloc(vlen + 1);
	snprintf(dup, vlen + 1, "%.*s", (int) vlen, value);

	ini_close(ini);
	return dup;
}

static void write_to_ini(struct load_store_params *params, const char *dev_name,
		size_t name_len, const char *attr, const char *val, size_t len)
{
	FILE *f = params->f;

	if (params->is_debug)
		fwrite("debug.", 1, sizeof("debug.") - 1, f);
	fwrite(dev_name, 1, name_len, f);
	fwrite(".", 1, 1, f);
	fwrite(attr, 1, strlen(attr), f);
	fwrite(" = ", 1, sizeof(" = ") - 1, f);
	fwrite(val, 1, len - 1, f);
	fwrite("\n", 1, 1, f);
}

static int save_to_ini_dev_cb(struct iio_device *dev,
		const char *attr, const char *val, size_t len, void *d)
{
	struct load_store_params *params = (struct load_store_params *) d;
	const char *dev_name = iio_device_get_name(dev);
	size_t name_len = dev_name ? strlen(dev_name) : 0;

	if (attr_in_whitelist(attr, dev_name, name_len, params->is_debug,
				params->whitelist, params->list_len))
		write_to_ini(params, dev_name, name_len, attr, val, len);
	return 0;
}

static int save_to_ini_chn_cb(struct iio_channel *chn,
		const char *attr, const char *val, size_t len, void *d)
{
	struct load_store_params *params = (struct load_store_params *) d;
	const char *dev_name = iio_device_get_name(params->dev);
	size_t name_len = dev_name ? strlen(dev_name) : 0;

	attr = iio_channel_attr_get_filename(chn, attr);
	if (attr_in_whitelist(attr, dev_name, name_len, false,
				params->whitelist, params->list_len))
		write_to_ini(params, dev_name, name_len, attr, val, len);
	return 0;
}

void save_to_ini(FILE *f, const char *driver_name, struct iio_device *dev,
		const char * const *whitelist, size_t list_len)
{
	unsigned int i;
	struct load_store_params params = {
		.dev = dev,
		.whitelist = whitelist,
		.list_len = list_len,
		.is_debug = false,
		.f = f,
	};

	write_driver_name_to_ini(f, driver_name);

	for (i = 0; i < iio_device_get_channels_count(dev); i++)
		iio_channel_attr_read_all(iio_device_get_channel(dev, i),
				save_to_ini_chn_cb, &params);
	iio_device_attr_read_all(dev, save_to_ini_dev_cb, &params);

	params.is_debug = true;
	iio_device_debug_attr_read_all(dev, save_to_ini_dev_cb, &params);
}

int foreach_in_ini(const char *ini_file,
		int (*cb)(int, const char *, const char *, const char *))
{
	int ret = 0;
	const char *name, *key, *value;
	size_t nlen, klen, vlen;
	struct INI *ini = ini_open(ini_file);
	if (!ini)
		return -1;

	while (ini_next_section(ini, &name, &nlen) > 0) {
		char *n = malloc(nlen + 1);
		if (!n)
			goto err_ini_close;

		snprintf(n, nlen + 1, "%.*s", (int) nlen, name);

		while (ini_read_pair(ini, &key, &klen, &value, &vlen) > 0) {
			int line = ini_get_line_number(ini, key);
			char *v, *k = malloc(klen + 1);
			if (!k) {
				free(n);
				goto err_ini_close;
			}

			v = malloc(vlen + 1);
			if (!v) {
				free(k);
				free(n);
				goto err_ini_close;
			}

			snprintf(k, klen + 1, "%.*s", (int) klen, key);
			snprintf(v, vlen + 1, "%.*s", (int) vlen, value);

			ret = cb(line, n, k, v);

			/* only needed when debugging - this should be done in each section
			if (ret < 0) {
				fprintf(stderr, "issue in '%s' file: Section:'%s' key:'%s' value:'%s'\n",
						ini_file, n, k, v);
			}
			*/

			free(k);
			free(v);

			if (ret < 0) {
				free(n);
				goto err_ini_close;
			}

			if (ret > 0) {
				ret = 0;
				break;
			}
		}

		free(n);
	}

err_ini_close:
	ini_close(ini);
	return ret;
}

/*
 * Types of loops that can be handled while parsing .ini files
 * INI_LOOP_SEQ - Loops through a sequence of numbers. Ini syntax:
 * <SEQ> [var] [first] [increment] [last]
 * </SEQ>
 *
 * INI_LOOP_FOR - Loops through a given list of values. Ini syntax:
 * <FOR> [var] in {space-separated values}
 * </FOR>
 *
 */
enum ini_loop_types {
	INI_LOOP_SEQ,
	INI_LOOP_FOR,
};

/*
 * Structure of a ini loop that stores loop properties and attributes.
 * type - The type of the loop. See enum ini_loop_types.
 * var - The variable of the loop that is used as an iterator.
 * end_loop - The keyword used to mark the end of the loop.
 * first - first iteration to start from.
 * inc - incrementation step.
 * last - last iteration.
 * i - Holds the current iteration of the loop.
 * for_values - Use only by the <FOR> loop to store the list of values that the
 *              loop will iterate through.
 */
struct ini_loop {
	int type;
	char var[128];
	char end_loop[128];
	long long first;
	long long inc;
	long long last;
	long long i;
	gchar **for_values;
};

/*
 * Structure of a set of parameters that are used when parsing and expanding
 * an ini loop structure and all its inner loops.
 * in_file - input ini file
 * out_file - output (expanded) ini file
 * ini_loops - list of loop and its inner loops
 * unclosed_loops - flag is set when one of the loops is left unclosed in the
 *                  input ini file.
 */
struct loops_parse_params {
	FILE *in_file;
	FILE *out_file;
	GSList *ini_loops;
	bool unclosed_loops;
};

static void loops_params_init(struct loops_parse_params *p, FILE *i, FILE *o)
{
	if (p) {
		p->in_file = i;
		p->out_file = o;
		p->ini_loops = NULL;
		p->unclosed_loops = false;
	} else {
		printf("Structure to init in %s is invalid\n", __func__);
	}
}

/*
 * Check if a char array starts with a keyword and extracts the keyword.
 * A keyword is any char array between '<' and '>' characters and is found at
 * the beginning of the array and does not start with '/' character.
 */
static bool ini_line_begins_with_keyword(char *line, char *extracted_keyword)
{
	char *keyword_start = strstr(line, "<");
	char *keyword_end = strstr(line, ">");
	bool ret = false;

	if (keyword_start && (keyword_start - line == 0) && keyword_end &&
						*(++keyword_start) != '/') {
		strncpy(extracted_keyword, keyword_start,
				keyword_end - keyword_start);
		extracted_keyword[keyword_end - keyword_start] = 0;
		ret = true;
	}

	return ret;
}

/*
 * Create and initialize a new ini_loop structure base on the supplied data.
 * buf_with_loop - array containing loop instruction and all its parameters.
 * loop_name - keyword used to identify the type of loop.
 */
static struct ini_loop * ini_loop_new(char *buf_with_loop, char *loop_name)
{
	struct ini_loop *loop = NULL;
	long long first, inc, last;
	int loop_type;
	char var[128];
	gchar *for_raw_list = NULL;
	gchar **for_values = NULL;
	int ret;

	/* Check loop type. Base on that gather up all of it's parameters */
	if (!strncmp(loop_name, "SEQ", sizeof("SEQ") - 1)) {
		loop_type = INI_LOOP_SEQ;

		ret = sscanf(buf_with_loop, "<SEQ> %s %lli %lli %lli",
				var, &first, &inc, &last);
		if (ret != 4) {
			ret = -EINVAL;
			fprintf(stderr, "Unrecognized SEQ line\n");
			goto err_close;
		}
	} else if (!strncmp(loop_name, "FOR", sizeof("FOR") - 1)) {
		loop_type = INI_LOOP_FOR;

		ret = sscanf(buf_with_loop, "<FOR> %s in {", var);
		for_raw_list = g_strstr_len(buf_with_loop, -1, "{");
		if (!for_raw_list) {
			ret = -EINVAL;
			fprintf(stderr, "Unrecognized FOR line\n");
			goto err_close;
		}
		char *s = g_strdup(for_raw_list);
		s = g_strstrip(g_strdelimit(s, "{}\n", ' '));
		for_values = g_strsplit_set(s, " {}\n", -1);
		g_free(s);
		first = 0;
		inc = 1;
		last = g_strv_length(for_values) - 1;
	} else {
		ret = -EINVAL;
		fprintf(stderr, "Unrecognized %s loop keyword\n", loop_name);
		goto err_close;
	}

	/* Store all necessary parameters of a loop */
	loop = calloc(1, sizeof(struct ini_loop));
	if (!loop) {
		fprintf(stderr, "%s is %s", strerror(errno), __func__);
		goto err_close;
	}
	loop->type = loop_type;
	loop->first = first;
	loop->inc = inc;
	loop->last = last;
	if (snprintf(loop->var, sizeof(loop->var), "<%s>", var) == sizeof(loop->var))
		goto err_close;

	if (snprintf(loop->end_loop, sizeof(loop->end_loop), "</%s>", loop_name) == sizeof(loop->end_loop))
		goto err_close;

	loop->for_values = for_values;

err_close:

	return loop;
}

/*
 * Get the current iteration that the loop is at.
 */
static char * ini_loop_get_iteration(struct ini_loop *loop)
{
	char *iteration;

	switch (loop->type) {
	case INI_LOOP_SEQ:
		iteration = g_strdup_printf(LONG_LONG_FORMAT, loop->i);
		break;
	case INI_LOOP_FOR:
		iteration = g_strdup(loop->for_values[loop->i]);
		break;
	default:
		iteration = NULL;
	}

	return iteration;
}

/*
 * Expand a <loop>..</loop> structure and any other inner loop structures.
 */
static int loop_expand(struct loops_parse_params *parse_params,
		char *buf_with_loop, char *loop_name)
{
	char *replace, *eol;
	char buf[1024];
	char inner_loop[128];
	size_t tmplen;
	fpos_t pos;
	long long i, first, inc, last;
	int ret = 0;
	struct ini_loop *iniloop;
	bool unclosed_loop = true;

	iniloop = ini_loop_new(buf_with_loop, loop_name);
	if (!iniloop)
		goto err_close;

	parse_params->ini_loops = g_slist_prepend(parse_params->ini_loops,
					iniloop);

	first = iniloop->first;
	inc = iniloop->inc;
	last = iniloop->last;
	fgetpos(parse_params->in_file, &pos);
	for (i = first; inc > 0 ? i <= last : i >= last; i = i + inc) {
		fsetpos(parse_params->in_file, &pos);
		iniloop->i = i;

		while (fgets(buf, sizeof(buf), parse_params->in_file) != NULL) {
			if (ini_line_begins_with_keyword(buf, inner_loop)) {
				ret = loop_expand(parse_params, buf,
					inner_loop);
				if (ret < 0) {
					goto err_close;
				}
			} else {
				if (!strncmp(buf, iniloop->end_loop,
						strlen(iniloop->end_loop))) {
					unclosed_loop = false;
					break;
				}

				GSList *list, *node;
				struct ini_loop *loop;

				list = parse_params->ini_loops;
				replace = NULL;
				for (node = list; node;
						node = g_slist_next(node)) {
					loop = (struct ini_loop *)node->data;
					replace = strstr(buf, loop->var);
					if (replace)
						break;
				}
				if (!replace) {
					fprintf(parse_params->out_file, "%s",
						buf);
					continue;
				}

				tmplen = strlen(iniloop->var);
				eol = strchr(buf, '\0');
				gchar *it = ini_loop_get_iteration(loop);
				fprintf(parse_params->out_file, "%.*s%s%.*s",
					(int) (long) (replace - buf), buf,
					it, (int) (eol - replace - tmplen),
					(char *)((uintptr_t) replace + tmplen));
				if (it)
					g_free(it);
			}
		}
	}

err_close:

	if (iniloop) {
		parse_params->ini_loops = g_slist_remove(
					parse_params->ini_loops, iniloop);
		if (iniloop->for_values)
			g_strfreev(iniloop->for_values);
		free(iniloop);
	}

	if (unclosed_loop) {
		parse_params->unclosed_loops = true;
		ret = -EINVAL;
	}

	return ret;
}

int ini_unroll(const char *input, const char *output)
{
	FILE *in, *out;
	char buf[1024];
	char loop_name[128];
	int ret = 0;
	struct loops_parse_params loops_params;

	in = fopen(input, "r");
	out = fopen (output, "w");

	if (!in) {
		ret = -errno;
		fprintf(stderr, "Failed to open %s : %s\n", input,
			strerror(-ret));
		goto err_close;
	}

	if (!out) {
		ret = -errno;
		fprintf(stderr, "Failed to open %s : %s\n", output,
			strerror(-ret));
		goto err_close;
	}

	loops_params_init(&loops_params, in, out);
	while (fgets(buf, sizeof(buf), in) != NULL) {
		if (!buf[0])
			continue;

		if (!ini_line_begins_with_keyword(buf, loop_name)) {
			fprintf(out, "%s", buf);
			continue;
		}
		if (!strcmp(loop_name, "COMMENT")) {
			while (fgets(buf, sizeof(buf), in) != NULL) {
				if (!strncmp(buf, "</COMMENT>", strlen("</COMMENT>")))
					break;
			}
			continue;
		}

		ret = loop_expand(&loops_params, buf, loop_name);
		if (ret < 0) {
			if (loops_params.unclosed_loops)
				printf("loop isn't closed in %s\n", input);
			break;
		}
	}

	err_close:
	fclose(in);
	fclose(out);
	return ret;
}

void write_driver_name_to_ini(FILE *f, const char *driver_name)
{
	if (driver_name) {
		fwrite("\n[", 1, 2, f);
		fwrite(driver_name, 1, strlen(driver_name), f);
		fwrite("]\n", 1, 2, f);
	}
}
