#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <curl/curl.h>
#include <jansson.h>

#include "phone_home.h"

typedef struct _sized_buffer {
	char *buffer;
	size_t size;
} SizedBuffer;


static size_t git_data_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	SizedBuffer *sbuf = (SizedBuffer *)userdata;
	size_t old_size, new_buf_size = size * nmemb;
	char *r_buf;

	if (ptr) {
		old_size = sbuf->size;
		r_buf = (char *)realloc(sbuf->buffer, old_size + new_buf_size);
		if (r_buf) {
			memcpy(r_buf + old_size, ptr, new_buf_size);
			sbuf->buffer = r_buf;
			sbuf->size += new_buf_size;
		} else {
			printf("buffer realloc failed\n");
		}
	}

	return new_buf_size;
}

static CURLcode git_request(const char *url, char **out_data)
{
	CURL *c_handle;
	CURLcode c_ret = CURLE_OK;
	SizedBuffer sized_buffer = { NULL, 0 };
	long resp_code;

	c_handle = curl_easy_init();
	if (!c_handle) {
		printf("curl_easy_init() failed: %s\n",
			curl_easy_strerror(c_ret));
		goto cleanup;
	}

	c_ret = curl_easy_setopt(c_handle, CURLOPT_URL, url);
	if (c_ret != CURLE_OK) {
		printf("curl_easy_setop(with CURLOPT_URL) failed: %s\n",
			   curl_easy_strerror(c_ret));
		goto cleanup;
	}

	c_ret = curl_easy_setopt(c_handle, CURLOPT_USERAGENT, "iio-oscilloscope");
	if (c_ret != CURLE_OK) {
		printf("curl_easy_setop(with CURLOPT_USERAGENT) failed: %s\n",
			   curl_easy_strerror(c_ret));
		goto cleanup;
	}

	c_ret = curl_easy_setopt(c_handle, CURLOPT_WRITEFUNCTION, git_data_write_cb);
	if (c_ret != CURLE_OK) {
		printf("curl_easy_setop(with CURLOPT_WRITEFUNCTION failed: %s\n",
			   curl_easy_strerror(c_ret));
		goto cleanup;
	}

	c_ret = curl_easy_setopt(c_handle, CURLOPT_WRITEDATA, &sized_buffer);
	if (c_ret != CURLE_OK) {
		printf("curl_easy_setop(with CURLOPT_WRITEDATA) failed: %s\n",
			   curl_easy_strerror(c_ret));
		goto cleanup;
	}

	c_ret = curl_easy_perform(c_handle);
	if (c_ret != CURLE_OK) {
		printf("curl_easy_perform() failed: %s\n", curl_easy_strerror(c_ret));
		goto cleanup;
	}

	c_ret = curl_easy_getinfo(c_handle, CURLINFO_RESPONSE_CODE, &resp_code);
	if (c_ret != CURLE_OK) {
		printf("curl_easy_getinfo() failed: %s\n", curl_easy_strerror(c_ret));
		goto cleanup;
	}

	if (resp_code != 200) {
		printf("Request returns code: %ld\n", resp_code);
		if (sized_buffer.buffer) {
			free(sized_buffer.buffer);
			sized_buffer.buffer = NULL;
		}
		goto cleanup;
	}

	if (sized_buffer.buffer) {
		char *r_buf;
		r_buf = (char *)realloc(sized_buffer.buffer, sized_buffer.size + 1);
		if (r_buf) {
			sized_buffer.buffer = r_buf;
			sized_buffer.buffer[sized_buffer.size] = '\0';
		} else {
			free(sized_buffer.buffer);
			sized_buffer.buffer = NULL;
		}
	}

	*out_data = sized_buffer.buffer;

cleanup:
	if (c_handle) {
		curl_easy_cleanup(c_handle);
		c_handle = NULL;
	}

	return c_ret;
}

static json_t* decode_text(const char *text)
{
	json_t *root = NULL;
	json_error_t err;

	if (!text)
		goto exit;

	root = json_loads(text, 0, &err);
	if (!root) {
		printf("json_loads() failed: %s\n", err.text);
		goto exit;
	}

	if (!json_is_array(root)) {
		printf("json_is_array() failed\n");
		json_decref(root);
		root = NULL;
	}

exit:
	return root;
}

static json_t * get_latest_release(json_t *root)
{
	json_t *elem, *publish_at;
	json_t *latest_release = NULL;
	const char *newest_date = NULL;
	size_t i;

	for (i = 0; i < json_array_size(root); i++) {
		elem = json_array_get(root, i);
		if (!json_is_object(elem)) {
			printf("json_is_object(elem) failed\n");
			break;
		}

		publish_at = json_object_get(elem, "published_at");
		if (!json_is_string(publish_at)) {
			printf("json_is_string(publish_at) failed\n");
			break;
		}

		if (!newest_date) {
			newest_date = json_string_value(publish_at);
			latest_release = elem;
		}
		else if (strcmp(newest_date, json_string_value(publish_at)) < 0) {
			newest_date = json_string_value(publish_at);
			latest_release = elem;
		}
	}

	return latest_release;
}

static json_t * decode_url_feedback(const char *url)
{
	json_t *j_root = NULL;
	char *data = NULL;
	CURLcode c_ret;

	c_ret = git_request(url, &data);
	if (c_ret != CURLE_OK) {
		printf("git_request from %s failed\n", url);
		goto fail;
	}
	if (!data) {
		printf("Could not get data from %s", url);
		goto fail;
	}

	j_root = decode_text(data);
	free(data);

fail:
	return j_root;
}

Release * release_new(void)
{
	return calloc(1, sizeof(Release));
}

void release_dispose(Release *_this)
{
	if (!_this)
		return;

	if (_this->name)
		free(_this->name);
	if (_this->build_date)
		free(_this->build_date);
	if (_this->commit)
		free(_this->commit);
	if (_this->url)
		free(_this->url);
	if (_this->windows_dld_url)
		free(_this->windows_dld_url);
}

Release * release_get_latest(void)
{
	Release *release = NULL;
	const char *SERVER_URL = "https://api.github.com/repos/analogdevicesinc/iio-oscilloscope/releases";
	json_t *j_root, *j_release;
	bool release_abort = false;

	j_root = decode_url_feedback(SERVER_URL);
	if (!j_root) {
		printf("Could not decode data about git releases\n");
		goto cleanup_and_fail;
	}

	j_release = get_latest_release(j_root);
	if (!j_release) {
		printf("No release found\n");
		goto cleanup_and_fail;
	}

	release = release_new();
	if (!release) {
		printf("%s\n", strerror(errno));
		goto cleanup_and_fail;
	}

	release->name = strdup(json_string_value(json_object_get(j_release, "name")));
	release->build_date = strdup(json_string_value(json_object_get(j_release, "created_at")));
	release->url = strdup(json_string_value(json_object_get(j_release, "html_url")));

	/* Get the release SHA commit */
	json_t *j_tags, *tag, *name, *commit;
	const char *tag_name;
	size_t i;

	tag_name = json_string_value(json_object_get(j_release, "tag_name"));
	j_tags = decode_url_feedback("https://api.github.com/repos/analogdevicesinc/iio-oscilloscope/tags");
	if (!j_tags) {
		printf("Could not decode data about git tags\n");
		release_abort = true;
		goto cleanup_and_fail;
	}

	for (i = 0; i < json_array_size(j_tags); i++) {
		tag = json_array_get(j_tags, i);
		if (!json_is_object(tag))
			break;
		name = json_object_get(tag, "name");
		if (!json_is_string(name))
			break;
		if (tag_name && !strcmp(json_string_value(name), tag_name)) {
			commit = json_object_get(tag, "commit");
			if (!json_is_object(commit))
				break;
			release->commit = strdup(json_string_value(
					json_object_get(commit, "sha")));
		}
	}
	if (!release->commit) {
		printf("Could not find release SHA commit\n");
		release_abort = true;
		goto cleanup_and_fail;
	}

	/* Get download link for the windows build */
	json_t *assets, *w_build;
	assets = json_object_get(j_release, "assets");
	if (!assets) {
		release_abort = true;
		goto cleanup_and_fail;
	}
	w_build = json_array_get(assets, 0);
	if (!w_build) {
		release_abort = true;
		goto cleanup_and_fail;
	}
	release->windows_dld_url = strdup(json_string_value(json_object_get(w_build, "browser_download_url")));

cleanup_and_fail:
	if (release_abort) {
		release_dispose(release);
		release = NULL;
	}
	json_decref(j_root);

	return release;
}

int phone_home_init(void)
{
	if (curl_global_init(CURL_GLOBAL_DEFAULT)) {
		printf("curl_global_init() failed!\n");
		return 0;
	}

	return 1;
}

void phone_home_terminate(void)
{
	curl_global_cleanup();
}
