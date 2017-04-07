/**
 * Copyright (C) 2013-2014 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/

#ifndef __MATH_EXPRESSION_GENERATOR_H__
#define __MATH_EXPRESSION_GENERATOR_H__

#ifdef linux
#include <glib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <ftw.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <unistd.h>
#endif

#define MATH_OBJECT_FILES_DIR "math_expressions"
#define MATH_EXPRESSION_BASE_FILE "math_expression"
#define MATH_FUNCTION_NAME "expression_function"

typedef void (*math_function)(float ***channels_data, float *out_data, unsigned long long chn_sample_cnt);

#ifdef linux
static int remove_cb(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
	return remove(fpath);
}

/* Recursively remove the given path from the filesystem. */
static int recursive_remove(char *dirpath)
{
	return nftw(dirpath, remove_cb, 64, FTW_DEPTH | FTW_PHYS);
}

static gboolean eval(const GMatchInfo *info, GString *res, gpointer data)
{
	gchar *match;
	gchar *replace;
	int index;
	char *pos;

	match = g_match_info_fetch(info, 0);
	if (!match)
		return FALSE;

	for (pos = match; *pos; pos++) {
		if (g_ascii_isdigit(*pos))
			break;
	}
	if ((size_t)(pos - match) < strlen(match))
		index = atoi(pos);
	else
		index = 0;

	replace = g_strdup_printf("(*channels_data[%d])[i]", index);
	g_string_append(res, replace);
	g_free(replace);
	g_free(match);

	return FALSE;
}

static char * string_replace(const char * string, const char *pattern,
			const char *replacement, GRegexEvalCallback eval)
{
	GRegex *rex;
	gchar *result;

	rex = g_regex_new(pattern, 0, 0, NULL);
	if (eval)
		result = g_regex_replace_eval(rex, string, -1, 0, 0, eval, NULL, NULL);
	else
		result = g_regex_replace_literal(rex, string, -1, 0, replacement, 0, NULL);
	g_regex_unref(rex);

	return result;
}

static char * c_file_create(const char *user_expression, GSList *basenames)
{
	char *base_filename, *open_path;
	FILE *fp;
	struct stat st;
	static unsigned long long file_count = 0;

	if (!user_expression) {
		fprintf(stderr, "NULL user_expression parameter in %s", __func__);
		return NULL;
	}
	if (stat(MATH_OBJECT_FILES_DIR, &st) == -1) {
		mkdir(MATH_OBJECT_FILES_DIR, S_IRWXU|S_IRWXG|S_IRWXO);
	}

	base_filename = g_strdup_printf("%s_%llu",
		MATH_EXPRESSION_BASE_FILE, file_count++);
	open_path = g_strdup_printf("%s/%s.c",
		MATH_OBJECT_FILES_DIR, base_filename);
	fp = fopen(open_path, "w+");
	g_free(open_path);
	if (!fp) {
		perror(base_filename);
		g_free(base_filename);
		return NULL;
	}

	char *s1, *s2;
	GSList *node;
	char *buf, *old_expr, *new_expr = NULL;

	old_expr = g_strdup(user_expression);
	for (node = basenames; node; node = g_slist_next(node)) {
		buf = g_strdup_printf("(%s[0-9]+)(\\w*)", (char *)node->data);
		new_expr = string_replace(old_expr, buf, NULL, eval);
		g_free(buf);
		g_free(old_expr);
		old_expr = new_expr;
	}
	if (new_expr)
		s1 = new_expr;
	else
		s1 = g_strdup(user_expression);

	s2 = string_replace(s1, "Index", "i", NULL);
	g_free(s1);
	s1 = string_replace(s2, "PreviousValue", "(i > 0 ? out_data[i  -1] : 0)", NULL);
	s2 = string_replace(s1, "SampleCount", "chn_sample_cnt", NULL);
	g_free(s1);

	fprintf(fp, "#include <math.h>\n");
	fprintf(fp, "#define max(a,b) \
		({ __typeof__ (a) _a = (a); \
		__typeof__ (b) _b = (b); \
		_a > _b ? _a : _b; })\n \
		 #define min(a,b) \
		({ __typeof__ (a) _a = (a); \
		 __typeof__ (b) _b = (b); \
		 _a < _b ? _a : _b; })\n");
	fprintf(fp, "\n");
	fprintf(fp, "void %s(float ***channels_data, float *out_data, unsigned long long chn_sample_cnt)\n", MATH_FUNCTION_NAME);
	fprintf(fp, "{\n");
	fprintf(fp, "\tunsigned long long i;\n\n");
	fprintf(fp, "\tfor (i = 0; i < chn_sample_cnt; i++) {\n");
	fprintf(fp, "\tout_data[i] = %s;\n", s2);
	fprintf(fp, "\t}\n");
	fprintf(fp, "}\n");

	fclose(fp);
	g_free(s2);

	return base_filename;
}

static int shared_object_compile(char *base_filename)
{
	char *pcommand, *object_path;
	FILE *pstream;
	int ret;

	pcommand = g_strdup_printf("gcc -c -Wall -Werror -fpic %s/%s.c -o %s/%s.o",
		MATH_OBJECT_FILES_DIR, base_filename, MATH_OBJECT_FILES_DIR, base_filename);
	pstream = popen(pcommand, "w");
	g_free(pcommand);
	if (!pstream) {
		perror("Error compiling math expression");
		return EXIT_FAILURE;
	}
	pclose(pstream);

	object_path = g_strdup_printf("./%s/%s.o",
		MATH_OBJECT_FILES_DIR, base_filename);
	ret = access(object_path, F_OK);
	g_free(object_path);
	if (ret != 0)
		return EXIT_FAILURE;

	pcommand = g_strdup_printf("gcc -shared -o %s/%s.so %s/%s.o",
		MATH_OBJECT_FILES_DIR, base_filename, MATH_OBJECT_FILES_DIR, base_filename);
	pstream = popen(pcommand, "w");
	g_free(pcommand);
	if (!pstream) {
		perror("Error creating math expression library");
		return EXIT_FAILURE;
	}
	pclose(pstream);

	object_path = g_strdup_printf("./%s/%s.so",
		MATH_OBJECT_FILES_DIR, base_filename);
	ret = access(object_path, F_OK);
	g_free(object_path);
	if (ret != 0)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
#endif

math_function math_expression_get_math_function(const char *expression_txt,
	void **lib_handler, GSList *basenames)
{
#ifdef linux
	math_function math_fn;
	char *base_filename, *dlopen_path;
	int ret;

	base_filename = c_file_create(expression_txt, basenames);
	if (!base_filename)
		return NULL;

	ret = shared_object_compile(base_filename);
	if (ret == EXIT_FAILURE) {
		goto FAILED_SO;
	}

	dlopen_path = g_strdup_printf("./%s/%s.so",
		MATH_OBJECT_FILES_DIR, base_filename);
	*lib_handler = dlopen(dlopen_path, RTLD_LOCAL | RTLD_LAZY);
	g_free(dlopen_path);
	if (!*lib_handler) {
		fprintf(stderr, "%s\n", dlerror());
		goto FAILED_SO;
	}

	math_fn = dlsym(*lib_handler, MATH_FUNCTION_NAME);
	if (!math_fn) {
		fprintf(stderr, "Failed to load %s symbol\n", MATH_FUNCTION_NAME);
	}

	return math_fn;

FAILED_SO:
	g_free(base_filename);
#endif
	return NULL;
}

void math_expression_close_lib_handler(void *lib_handler)
{
#ifdef linux
	if (lib_handler)
		dlclose(lib_handler);
#endif
}

void math_expression_objects_clean(void)
{
#ifdef linux
	if (recursive_remove(MATH_OBJECT_FILES_DIR) != 0 && errno != ENOENT)
		fprintf(stderr, "Can't remove %s: %s\n", MATH_OBJECT_FILES_DIR, strerror(errno));
#endif
}

#endif /* __MATH_EXPRESSION_GENERATOR_H__ */
