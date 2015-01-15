/**
 * Copyright (C) 2013-2014 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/

#ifndef __MATH_EXPRESSION_GENERATOR_H__
#define __MATH_EXPRESSION_GENERATOR_H__

#include <glib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <unistd.h>

#define MATH_OBJECT_FILES_DIR "math_expressions"
#define MATH_EXPRESSION_BASE_FILE "math_expression"
#define MATH_FUNCTION_NAME "expression_function"

typedef void (*math_function)(float ***channels_data, float *out_data, unsigned long long chn_sample_cnt);

static gboolean eval(const GMatchInfo *info, GString *res, gpointer data)
{
	gchar *match;
	gchar *replace;
	int index;

	match = g_match_info_fetch(info, 0);
	if (!match)
		return FALSE;
	index = atoi(match + strlen("voltage"));
	replace = g_strdup_printf("(*channels_data[%d])[i]", index);
	g_string_append(res, replace);
	g_free(replace);
	g_free(match);

	return FALSE;
}

static char * c_file_create(const char *user_expression)
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
		fprintf(stderr, "%s", strerror(errno));
		g_free(base_filename);
		return NULL;
	}

	GRegex *rex;
	gchar *result;

	rex = g_regex_new("voltage[0-9]+", 0, 0, NULL);
	result = g_regex_replace_eval(rex, user_expression, -1, 0, 0, eval, NULL, NULL);
	g_regex_unref(rex);

	fprintf(fp, "#include <math.h>\n");
	fprintf(fp, "\n");
	fprintf(fp, "void %s(float ***channels_data, float *out_data, unsigned long long chn_sample_cnt)\n", MATH_FUNCTION_NAME);
	fprintf(fp, "{\n");
	fprintf(fp, "\tunsigned long long i;\n\n");
	fprintf(fp, "\tfor (i = 0; i < chn_sample_cnt; i++) {\n");
	fprintf(fp, "\tout_data[i] = %s;\n", result);
	fprintf(fp, "\t}\n");
	fprintf(fp, "}\n");

	fclose(fp);
	g_free(result);

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
		fprintf(stderr, "%s", strerror(errno));
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
		fprintf(stderr, "%s", strerror(errno));
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

math_function math_expression_get_math_function(const char *expression_txt, void **lib_handler)
{
	math_function math_fn;
	char *base_filename, *dlopen_path;
	int ret;

	base_filename = c_file_create(expression_txt);
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
	return NULL;
}

void math_expression_close_lib_handler(void *lib_handler)
{
	if (lib_handler)
		dlclose(lib_handler);
}

void math_expression_objects_clean(void)
{
	FILE *pstream;
	char *pcommand;

	pcommand = g_strdup_printf("rm -rf %s", MATH_OBJECT_FILES_DIR);
	pstream = popen(pcommand, "w");
	g_free(pcommand);
	if (!pstream) {
		fprintf(stderr, "%s", strerror(errno));
		return;
	}
	pclose(pstream);
}

#endif /* __MATH_EXPRESSION_GENERATOR_H__ */
