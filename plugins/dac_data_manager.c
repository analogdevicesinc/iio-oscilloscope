#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <ctype.h>
#include <sys/stat.h>
#ifdef __linux__
#include <sys/utsname.h>
#endif
#include <matio.h>
#include <unistd.h>

#include "dac_data_manager.h"
#include "../iio_widget.h"
#include "../osc.h"

#define I_CHANNEL 'I'
#define Q_CHANNEL 'Q'

#define FREQUENCY_SPIN_DIGITS 6
#define SCALE_SPIN_DIGITS 0
#define PHASE_SPIN_DIGITS 3

#define SCALE_MINUS_INFINITE -91

#define TX_NB_TONES 4
#define CHANNEL_NB_TONES 2

#define TX_T1_I 0
#define TX_T2_I 1
#define TX_T1_Q 2
#define TX_T2_Q 3

#define IIO_SPIN_SIGNAL "value-changed"
#define IIO_COMBO_SIGNAL "changed"

#define TX_CHANNEL_NAME 0
#define TX_CHANNEL_ACTIVE 1
#define TX_CHANNEL_REF_INDEX 2

#define WAVEFORM_TXT_INVALID_FORMAT 1
#define WAVEFORM_MAT_INVALID_FORMAT 2

extern bool dma_valid_selection(const char *device, unsigned mask, unsigned channel_count);

struct dds_tone {
	struct dds_channel *parent;

	unsigned number;
	struct iio_device *iio_dac;
	struct iio_channel *iio_ch;

	struct iio_widget iio_freq;
	struct iio_widget iio_scale;
	struct iio_widget iio_phase;

	double scale_state;

	gint dds_freq_hid;
	gint dds_scale_hid;
	gint dds_phase_hid;

	GtkWidget *freq;
	GtkWidget *scale;
	GtkWidget *phase;
	GtkWidget *frame;
};

struct dds_channel {
	struct dds_tx *parent;

	char type;
	struct dds_tone t1;
	struct dds_tone t2;

	GtkWidget *frame;
};

struct dds_tx {
	struct dds_dac *parent;

	unsigned index;
	struct dds_channel ch_i;
	struct dds_channel ch_q;
	struct dds_tone *dds_tones[4];

	GtkWidget *frame;
	GtkWidget *dds_mode_widget;
};

struct dds_dac {
	struct dac_data_manager *parent;

	unsigned index;
	const char *name;
	struct iio_device *iio_dac;
	unsigned tx_count;
	struct dds_tx *txs;
	int dds_mode;
	unsigned tones_count;

	GtkWidget *frame;
};

struct dac_buffer {
	struct dac_data_manager *parent;

	char *dac_buf_filename;
	int scan_elements_count;
	struct iio_device *dac_with_scanelems;

	GtkWidget *frame;
	GtkWidget *buffer_fchooser_btn;
	GtkWidget *tx_channels_view;
	GtkWidget *scale;
	GtkTextBuffer *load_status_buf;
};

struct dac_data_manager {
	struct dds_dac dac1;
	struct dds_dac dac2;
	struct dac_buffer dac_buffer_module;

	struct iio_context *ctx;
	unsigned dacs_count;
	unsigned tones_count;
	unsigned alignment;
	bool hw_reported_alignment;
	GSList *dds_tones;
	bool scale_available_mode;
	double lowest_scale_point;
	bool dds_activated;
	bool dds_disabled;
	struct iio_buffer *dds_buffer;
	bool is_local;

	GtkWidget *container;
};

static bool tx_channels_check_valid_setup(struct dac_buffer *dbuf);

static const gdouble abs_mhz_scale = -1000000.0;
static const gdouble khz_scale = 1000.0;

static int compare_gain(const char *a, const char *b)
{
	double val_a, val_b;
	sscanf(a, "%lf", &val_a);
	sscanf(b, "%lf", &val_b);

	if (val_a < val_b)
		return -1;
	else if(val_a > val_b)
		return 1;
	else
		return 0;
}

static double db_full_scale_convert(double value, bool inverse)
{
	if (inverse) {
		if (value == 0)
			return -DBL_MAX;
		return (int)((20 * log10(value)) - 0.5);
	} else {
		if (value == SCALE_MINUS_INFINITE)
			return 0;
		return pow(10, value / 20.0);
	}
}

/* add backwards compat for <matio-1.5.0 */
#if MATIO_MAJOR_VERSION == 1 && MATIO_MINOR_VERSION < 5
typedef struct ComplexSplit mat_complex_split_t;
#endif

struct _complex_ref {
	double *re;
	double *im;
};

static double dac_offset_get_value(struct iio_device *dac)
{
	double offset;
	const char *dev_name;

	if (!dac)
		return 0.0;

	offset = 0.0;
	dev_name = iio_device_get_name(dac);
	if (!strcmp(dev_name, "cf-ad9122-core-lpc"))
		offset = 32767.0;

	return offset;
}

/*
 * Fill empty channels with copies of other channels
 * E.g. (data, NULL, NULL, NULL) becomes (data, data, data, data)
 * E.g. (data1, data2, NULL, NULL) becomes (data1, data2, data1, data2)
 * ...
 */
static void replicate_tx_data_channels(struct _complex_ref *data, int count)
{
	if (!data)
		return;

	if (count > 2)
		replicate_tx_data_channels(data, count / 2);

	int i, half = count / 2;

	/* Check if the second half of the array needs to be filled */
	if (data[half].re == NULL || data[half].im == NULL) {
		for (i = 0; i < half; i++) {
				data[half + i].re = data[i].re;
				data[half + i].im = data[i].im;
		}
	}
}

static unsigned short convert(double scale, float val, double offset)
{
	return (short) (val * scale + offset);
}

static int parse_wavefile_line(const char *line, double *vals,
	unsigned int max_num_vals)
{
	unsigned int n = 0;
	gchar *endptr;

	while (n < max_num_vals) {
		/* Skip white space */
		while (*line == ' ' || *line == '\t' || *line == ',')
			line++;

		if (*line == '\n' || *line == '\r' || *line == '\0')
			break;

		vals[n++] = g_ascii_strtod(line, &endptr);

		/*
		 * If endptr did not advance this means the value is not a
		 * number. In that case abort and return an error.
		 */
		if (errno || line == (const char *)endptr)
			return -1;
		line = (const char *)endptr;
	}

	return n;
}

static int analyse_wavefile(struct dac_data_manager *manager,
		const char *file_name, char **buf, int *count, int tx_channels, double full_scale)
{
	int ret, rep;
	unsigned int size, j, i = 0;
	double max = 0.0, val[8], scale = 0.0;
	double offset;
	char line[80];
	mat_t *matfp;
	matvar_t **matvars;


	FILE *infile = fopen(file_name, "r");

	*buf = NULL;

	if (infile == NULL)
		return -errno;

	offset = dac_offset_get_value(manager->dac1.iio_dac);

	if (fgets(line, 80, infile) != NULL) {
		if (strncmp(line, "TEXT", 4) == 0) {
			/* Unscaled samples need to be in the range +- 2047 */
			if (strncmp(line, "TEXTU", 5) == 0)
				scale = 16.0;	/* scale up to 16-bit */
			ret = sscanf(line, "TEXT%*c REPEAT %d", &rep);
			if (ret != 1) {
				rep = 1;
			}
			size = 0;
			while (fgets(line, 80, infile)) {
				ret = parse_wavefile_line(line, val, 8);
				if (ret == 0)
					continue;
				if (!(ret == 4 || ret == 2 || ret == 8)) {
					fclose(infile);
					fprintf(stderr, "ERROR: No 2, 4 or 8 columns of data inside the text file\n");
					return WAVEFORM_TXT_INVALID_FORMAT;
				}

				for (i = 0; i < (unsigned int) ret; i++)
					if (fabs(val[i]) > max)
						max = fabs(val[i]);

				size += tx_channels * 2;
			}

			size *= rep;
			if (scale == 0.0)
				scale = 32767.0 * full_scale / max;

			while ((size % manager->alignment) != 0)
				size *= 2;

			*buf = malloc(size);
			if (*buf == NULL)
				return -errno;

			unsigned long long *sample = *((unsigned long long **) buf);
			unsigned int *sample_32 = *((unsigned int **) buf);
			unsigned short *sample_16 = *((unsigned short **) buf);

			rewind(infile);

			if (fgets(line, 80, infile) != NULL) {
				if (strncmp(line, "TEXT", 4) == 0) {
					size = 0;
					i = 0;
					while (fgets(line, 80, infile)) {
						ret = parse_wavefile_line(line, val, 8);
						if (ret == 0)
							continue;

						for (j = 0; j < (unsigned int) rep; j++) {
							if (ret == 8 && tx_channels == 8) {
								sample[i++] = ((unsigned long long) convert(scale, val[3], offset) << 48) |
								((unsigned long long) convert(scale, val[2], offset) << 32) |
								((unsigned long long) convert(scale, val[1], offset) << 16) |
								((unsigned long long) convert(scale, val[0], offset) << 0);

								sample[i++] = ((unsigned long long) convert(scale, val[7], offset) << 48) |
								((unsigned long long) convert(scale, val[6], offset) << 32) |
								((unsigned long long) convert(scale, val[5], offset) << 16) |
								((unsigned long long) convert(scale, val[4], offset) << 0);

							} else if (ret == 4 && tx_channels >= 4) {
								sample[i++] = ((unsigned long long) convert(scale, val[3], offset) << 48) |
								    ((unsigned long long) convert(scale, val[2], offset) << 32) |
								    ((unsigned long long) convert(scale, val[1], offset) << 16) |
								    ((unsigned long long) convert(scale, val[0], offset) << 0);

								if (tx_channels == 8)
									sample[i++] = ((unsigned long long) convert(scale, val[3], offset) << 48) |
									((unsigned long long) convert(scale, val[2], offset) << 32) |
									((unsigned long long) convert(scale, val[1], offset) << 16) |
									((unsigned long long) convert(scale, val[0], offset) << 0);

							} else if (ret == 2 && tx_channels >= 4) {
								sample[i++] = ((unsigned long long) convert(scale, val[1], offset) << 48) |
								    ((unsigned long long) convert(scale, val[0], offset) << 32) |
								    ((unsigned long long) convert(scale, val[1], offset) << 16) |
								    ((unsigned long long) convert(scale, val[0], offset) << 0);

								if (tx_channels == 8)
									sample[i++] = ((unsigned long long) convert(scale, val[1], offset) << 48) |
									((unsigned long long) convert(scale, val[0], offset) << 32) |
									((unsigned long long) convert(scale, val[1], offset) << 16) |
									((unsigned long long) convert(scale, val[0], offset) << 0);

							} else if (ret > 1 && tx_channels == 2) {
								sample_32[i++] = ((unsigned int) convert(scale, val[1], offset) << 16) |
										((unsigned int) convert(scale, val[0], offset) << 0);
							} else if (ret > 1 && tx_channels == 1) {
								sample_16[i++] = convert(scale, val[0], offset);
							}

							size += tx_channels * 2;
						}
					}
				}
			}

			/* When we are in 1 TX mode it is possible that the number of bytes
			 * is not a multiple of 8, but only a multiple of 4. In this case
			 * we'll send the same buffer twice to make sure that it becomes a
			 * multiple of 8. (default manager->alignment)
			 */

			while ((size % manager->alignment) != 0) {
				memcpy(*buf + size, *buf, size);
				size += size;
			}

			fclose(infile);
			*count = size;

		} else {
			fclose(infile);
			ret = 0;
			/* Is it a MATLAB file?
			 * http://na-wiki.csc.kth.se/mediawiki/index.php/MatIO
			 */
			matfp = Mat_Open(file_name, MAT_ACC_RDONLY);
			if (matfp == NULL) {
				fprintf(stderr, "ERROR: Could not open %s as a matlab file\n", file_name);
				return WAVEFORM_MAT_INVALID_FORMAT;
			}

			bool complex_format = false;
			bool real_format = false;

			rep = 0;
			matvars = malloc(sizeof(matvar_t *) * tx_channels);

			while (rep < tx_channels && (matvars[rep] = Mat_VarReadNextInfo(matfp)) != NULL) {
				/* must be a vector */
				if (matvars[rep]->rank !=2 || (matvars[rep]->dims[0] > 1 && matvars[rep]->dims[1] > 1)) {
					fprintf(stderr, "ERROR: Data inside the matlab file must be a vector\n");
					free(matvars);
					return WAVEFORM_MAT_INVALID_FORMAT;
				}
				/* should be a double */
				if (matvars[rep]->class_type != MAT_C_DOUBLE) {
					fprintf(stderr, "ERROR: Data inside the matlab file must be of type double\n");
					free(matvars);
					return WAVEFORM_MAT_INVALID_FORMAT;
				}
/*
	printf("%s : %s\n", __func__, matvars[rep]->name);
	printf("  rank %d\n", matvars[rep]->rank);
	printf("  dims %d x %d\n", matvars[rep]->dims[0], matvars[rep]->dims[1]);
	printf("  data %d\n", matvars[rep]->data_type);
	printf("  class %d\n", matvars[rep]->class_type);
*/
				Mat_VarReadDataAll(matfp, matvars[rep]);

				if (matvars[rep]->isComplex) {
					mat_complex_split_t *complex_data = matvars[rep]->data;
					double *re, *im;
					re = complex_data->Re;
					im = complex_data->Im;

					for (j = 0; j < (unsigned int) matvars[rep]->dims[0] ; j++) {
						 if (fabs(re[j]) > max)
							 max = fabs(re[j]);
						 if (fabs(im[j]) > max)
							 max = fabs(im[j]);
					}
					complex_format = true;
				} else {
					double re;

					for (j = 0; j < (unsigned int) matvars[rep]->dims[0] ; j++) {
						re = ((double *)matvars[rep]->data)[j];
						if (fabs(re) > max)
							max = fabs(re);
					}
					real_format = true;
				}
				rep++;
			}
			rep--;

//	printf("read %i vars, length %i, max value %f\n", rep, matvars[rep]->dims[0], max);

			if (rep < 0) {
				fprintf(stderr, "ERROR: Could not find any valid data in %s\n", file_name);
				free(matvars);
				return WAVEFORM_MAT_INVALID_FORMAT;
			}

			if (max <= 1.0)
				max = 1.0;

			scale = 32767.0 * full_scale / max;

			size = matvars[0]->dims[0];

			for (i = 0; i <= (unsigned int) rep; i++) {
				if (size != (unsigned int) matvars[i]->dims[0]) {
					fprintf(stderr, "ERROR: Vector dimensions in the matlab file don't match\n");
					free(matvars);
					return WAVEFORM_MAT_INVALID_FORMAT;
				}
			}

			if (complex_format && real_format) {
				fprintf(stderr, "ERROR: Both complex and real data formats in the same matlab file are not supported\n");
				free(matvars);
				return WAVEFORM_MAT_INVALID_FORMAT;
			}

			*buf = malloc((size + 1) * tx_channels * 2);

			if (*buf == NULL) {
				free(matvars);
				return -errno;
			}

			*count = size * tx_channels * 2;

			unsigned long long *sample = *((unsigned long long **) buf);
			unsigned int *sample_32 = *((unsigned int **) buf);
			unsigned short *sample_16 = *((unsigned short **) buf);

			struct _complex_ref tx_data[4] = {{NULL, NULL}, {NULL, NULL}, {NULL, NULL}, {NULL, NULL}};
			mat_complex_split_t *complex_data[4];

			if (complex_format) {
				for (i = 0; i <= (unsigned int) rep; i++) {
					complex_data[i] = matvars[i]->data;
					tx_data[i].re = complex_data[i]->Re;
					tx_data[i].im = complex_data[i]->Im;
				}
			} else if (real_format) {
				for (i = 0; i <= (unsigned int) rep; i++) {
					if (i % 2)
						tx_data[i / 2].im = matvars[i]->data;
					else
						tx_data[i / 2].re = matvars[i]->data;
				}
			}
			replicate_tx_data_channels(tx_data, tx_channels);

			switch (tx_channels) {
			case 1:
				for (i = 0 ; i < size; i++) {
					sample_16[i] = convert(scale, tx_data[0].re[i], offset);
				}
				break;
			case 2:
				for (i = 0 ; i < size; i++) {
					sample_32[i] = ((unsigned int) convert(scale, tx_data[0].im[i], offset) << 16) |
						       ((unsigned int) convert(scale, tx_data[0].re[i], offset) << 0);
				}
				break;
			case 4:
				for (i = 0 ; i < size; i++) {
					sample[i] = ((unsigned long long) convert(scale, tx_data[1].im[i], offset) << 48) |
						    ((unsigned long long) convert(scale, tx_data[1].re[i], offset) << 32) |
							((unsigned long long) convert(scale, tx_data[0].im[i], offset) << 16) |
							((unsigned long long) convert(scale, tx_data[0].re[i], offset) << 0);
				 }
				break;
			case 8:
				for (i = 0, j = 0; i < size; i++) {
					sample[j++] = ((unsigned long long) convert(scale, tx_data[3].im[i], offset) << 48) |
						    ((unsigned long long) convert(scale, tx_data[3].re[i], offset) << 32) |
							((unsigned long long) convert(scale, tx_data[2].im[i], offset) << 16) |
							((unsigned long long) convert(scale, tx_data[2].re[i], offset) << 0);
					sample[j++] = ((unsigned long long) convert(scale, tx_data[1].im[i], offset) << 48) |
						    ((unsigned long long) convert(scale, tx_data[1].re[i], offset) << 32) |
							((unsigned long long) convert(scale, tx_data[0].im[i], offset) << 16) |
							((unsigned long long) convert(scale, tx_data[0].re[i], offset) << 0);
				}
				break;
			}

			for (j = 0; j <= (unsigned int) rep; j++) {
				Mat_VarFree(matvars[j]);
			}
			free(matvars);
			Mat_Close(matfp);
			return ret;
		}
	} else {
		fclose(infile);
		return -EINVAL;
	}
	return 0;
}

static gboolean scale_spin_button_output_cb(GtkSpinButton *spin, gpointer data)
{
	GtkAdjustment *adj;
	gchar *text;
	float value;

	adj = gtk_spin_button_get_adjustment(spin);
	value = gtk_adjustment_get_value(adj);
	if (value > gtk_adjustment_get_lower(adj))
		text = data ? g_strdup_printf("%1.1f dB", value) :
			g_strdup_printf("%d dB", (int) value);
	else
		text = g_strdup_printf("-Inf dB");
	gtk_entry_set_text(GTK_ENTRY(spin), text);
	g_free(text);

	return TRUE;
}

static int tx_enabled_channels_count(GtkTreeView *treeview, unsigned *enabled_mask)
{
	GtkTreeIter iter;
	gboolean enabled;
	int num_enabled = 0;
	int ch_pos = 0;

	GtkTreeModel *model = gtk_tree_view_get_model(treeview);
	gboolean next_iter = gtk_tree_model_get_iter_first(model, &iter);


	if (enabled_mask)
		*enabled_mask = 0;

	while (next_iter) {
		gtk_tree_model_get(model, &iter, TX_CHANNEL_ACTIVE, &enabled, -1);
		if (enabled) {
			num_enabled++;
			if (enabled_mask)
				*enabled_mask |= 1 << ch_pos;
		}
		ch_pos++;
		next_iter = gtk_tree_model_iter_next(model, &iter);
	}

	return num_enabled;
}

static void enable_dds_channels(struct dac_buffer *db)
{
	GtkTreeView *treeview = GTK_TREE_VIEW(db->tx_channels_view);
	GtkTreeIter iter;
	gboolean enabled;
	gint ch_index = 0;

	GtkTreeModel *model = gtk_tree_view_get_model(treeview);
	gboolean next_iter = gtk_tree_model_get_iter_first(model, &iter);

	while (next_iter) {
		gtk_tree_model_get(model, &iter, TX_CHANNEL_ACTIVE, &enabled,
			TX_CHANNEL_REF_INDEX, &ch_index, -1);

		struct iio_channel *channel = iio_device_get_channel(db->dac_with_scanelems, ch_index);

		if (enabled)
			iio_channel_enable(channel);
		else
			iio_channel_disable(channel);

		next_iter = gtk_tree_model_iter_next(model, &iter);
	}
}

static void enable_dds(struct dac_data_manager *manager, bool on_off)
{
	struct iio_device *dac1 = NULL;
	struct iio_device *dac2 = NULL;
	int ret;

	if (on_off == manager->dds_activated && !manager->dds_disabled)
		return;
	manager->dds_activated = on_off;

	if (manager->dds_buffer) {
		iio_buffer_destroy(manager->dds_buffer);
		manager->dds_buffer = NULL;
	}

	dac1 = manager->dac1.iio_dac;
	if (manager->dacs_count == 2)
		dac2 = manager->dac2.iio_dac;

	ret = iio_channel_attr_write_bool(iio_device_find_channel(dac1, "altvoltage0", true), "raw", on_off);
	if (ret < 0) {
		fprintf(stderr, "Failed to toggle DDS: %d\n", ret);
		return;
	}
	if (dac2) {
		ret = iio_channel_attr_write_bool(iio_device_find_channel(dac2, "altvoltage0", true), "raw", on_off);
		if (ret < 0) {
			fprintf(stderr, "Failed to toggle DDS: %d\n", ret);
			return;
		}
	}
}

static int process_dac_buffer_file (struct dac_data_manager *manager, const char *file_name, char **stat_msg)
{
	int ret, size = 0, s_size;
	double scale;
	/*
	struct stat st;
	*/
	char *buf = NULL, *tmp;
	/*
	FILE *infile;
	*/
	unsigned int buffer_channels = 0;

	if (manager->dds_buffer) {
		iio_buffer_destroy(manager->dds_buffer);
		manager->dds_buffer = NULL;
	}

	if (manager->is_local) {
#ifdef __linux__
		unsigned int major, minor;
		struct utsname uts;

		uname(&uts);
		sscanf(uts.release, "%u.%u", &major, &minor);
		if (major < 2 || (major == 3 && minor < 14)) {
			if (manager->dacs_count == 2)
				buffer_channels = 8;
			else if (manager->dac1.tx_count == 2)
				buffer_channels = 4;
			else
				buffer_channels = 2;
		} else {
			buffer_channels = tx_enabled_channels_count(GTK_TREE_VIEW(manager->dac_buffer_module.tx_channels_view), NULL);
		}
#endif
	} else {
		buffer_channels = tx_enabled_channels_count(GTK_TREE_VIEW(manager->dac_buffer_module.tx_channels_view), NULL);
	}


	if (g_str_has_suffix(file_name, ".bin")) {
		FILE *infile;
		struct stat st;

		/* Assume Binary format */
		stat(file_name, &st);
		buf = malloc(st.st_size);
		if (buf == NULL) {
			if (stat_msg)
				*stat_msg = g_strdup_printf("Internal memory allocation failed.");
			return -errno;
		}
		infile = fopen(file_name, "r");
		size = fread(buf, 1, st.st_size, infile);
		fclose(infile);
	} else {

		scale = db_full_scale_convert(gtk_spin_button_get_value(GTK_SPIN_BUTTON(manager->dac_buffer_module.scale)), false);
		ret = analyse_wavefile(manager, file_name, &buf, &size, buffer_channels, scale);
		if (ret < 0) {
			if (stat_msg)
				*stat_msg = g_strdup_printf("Error while parsing file: %s.", strerror(-ret));
			free(buf);
			return ret;
		} else if (ret > 0) {
			if (stat_msg)
				*stat_msg = g_strdup_printf("Invalid data format");
			free(buf);
			return -EINVAL;
		}
	}

	usleep(1000); /* FIXME: Temp Workaround needs some investigation */

	enable_dds(manager, false);
	enable_dds_channels(&manager->dac_buffer_module);

	struct iio_device *dac = manager->dac_buffer_module.dac_with_scanelems;

	s_size = iio_device_get_sample_size(dac);
	if (!s_size) {
		fprintf(stderr, "Unable to create buffer due to sample size");
		if (stat_msg)
			*stat_msg = g_strdup_printf("Unable to create buffer due to sample size");
		free(buf);
		return -EINVAL;
	}

	if (size % manager->alignment != 0 || size % s_size != 0) {
		fprintf(stderr, "Unable to create buffer due to sample size and number of samples");
		if (stat_msg)
			*stat_msg = g_strdup_printf("Unable to create buffer due to sample size and number of samples");
		free(buf);
		return -EINVAL;
	}

	manager->dds_buffer = iio_device_create_buffer(dac, size / s_size, true);
	if (!manager->dds_buffer) {
		fprintf(stderr, "Unable to create buffer: %s\n", strerror(errno));
		if (stat_msg)
			*stat_msg = g_strdup_printf("Unable to create iio buffer: %s", strerror(errno));
		free(buf);
		return -errno;
	}

	memcpy(iio_buffer_start(manager->dds_buffer), buf,
			iio_buffer_end(manager->dds_buffer) - iio_buffer_start(manager->dds_buffer));

	iio_buffer_push(manager->dds_buffer);
	free(buf);

	tmp = strdup(file_name);

	if (manager->dac_buffer_module.dac_buf_filename)
		free(manager->dac_buffer_module.dac_buf_filename);

	manager->dac_buffer_module.dac_buf_filename = tmp;

	if (stat_msg)
		*stat_msg = g_strdup_printf("Waveform loaded successfully.");

	return 0;
}

static bool tx_channels_check_valid_setup(struct dac_buffer *dbuf)
{
	struct iio_device *dac = dbuf->dac_with_scanelems;
	int enabled_channels;
	unsigned mask;

	enabled_channels = tx_enabled_channels_count(GTK_TREE_VIEW(dbuf->tx_channels_view), &mask);

	return (dma_valid_selection(iio_device_get_name(dac) ?: iio_device_get_id(dac),
			mask, dbuf->scan_elements_count) && enabled_channels > 0);
}

static void tx_channel_toggled(GtkCellRendererToggle* renderer, gchar* pathStr, struct dac_buffer *dbuf)
{
	GtkTreePath* path = gtk_tree_path_new_from_string(pathStr);
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean active;
	gint ch_index;

	if (!gtk_cell_renderer_get_sensitive(GTK_CELL_RENDERER(renderer)))
		return;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(dbuf->tx_channels_view));
	gtk_tree_model_get_iter(model, &iter, path);
	gtk_tree_model_get(model, &iter, TX_CHANNEL_ACTIVE, &active, TX_CHANNEL_REF_INDEX, &ch_index, -1);
	active = !active;
	gtk_tree_store_set(GTK_TREE_STORE(model), &iter, TX_CHANNEL_ACTIVE, active, -1);
	gtk_tree_path_free(path);
}

static void dac_buffer_config_file_set_cb (GtkFileChooser *chooser, struct dac_buffer *dbuf)
{
	dbuf->dac_buf_filename = gtk_file_chooser_get_filename(chooser);
	gtk_text_buffer_set_text(dbuf->load_status_buf, "", -1);
}

static void waveform_load_button_clicked_cb (GtkButton *btn, struct dac_buffer *dbuf)
{
	gchar *filename = dbuf->dac_buf_filename;
	gchar *status_msg;

	if (!filename || g_str_has_suffix(filename, "(null)")) {
		status_msg = g_strdup_printf("No file selected.");
	} else if (!g_str_has_suffix(filename, ".txt") && !g_str_has_suffix(filename, ".mat") && !g_str_has_suffix(filename, ".bin")) {
		status_msg = g_strdup_printf("Invalid file type. Please select a .txt, .bin or .mat file.");
	} else if (!tx_channels_check_valid_setup(dbuf)) {
		status_msg = g_strdup_printf("Invalid channel selection.");
	} else {
		process_dac_buffer_file(dbuf->parent, (const char *)filename, &status_msg);
	}

	gtk_text_buffer_set_text(dbuf->load_status_buf, status_msg, -1);
		g_free(status_msg);
}

static GtkWidget *spin_button_create(double min, double max, double step, unsigned digits)
{
	GtkWidget *spin_button;

	spin_button = gtk_spin_button_new_with_range(min, max, step);
	gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin_button), digits);

	return spin_button;
}

static GtkWidget *frame_with_table_create(const char *frm_title, unsigned rows, unsigned columns)
{
	GtkWidget *frame;
	GtkWidget *align;
	GtkWidget *table;

	frame = gtk_frame_new("");
	align = gtk_alignment_new(.5, .5, 1.0, 1.0);
	table = gtk_table_new(rows, columns, FALSE);

	gtk_label_set_markup(GTK_LABEL(gtk_frame_get_label_widget(GTK_FRAME(frame))), frm_title);
	gtk_alignment_set_padding(GTK_ALIGNMENT(align), 0, 0, 12, 0);

	gtk_container_add(GTK_CONTAINER(frame), align);
	gtk_container_add(GTK_CONTAINER(align), table);

	return frame;
}

static GtkWidget *gui_dds_mode_chooser_create(struct dds_tx *tx)
{
	GtkWidget *box;
	GtkWidget *dds_mode_lbl;
	GtkComboBoxText *dds_mode;
	bool no_buffer_support = !strcmp(tx->parent->name, "axi-ad9739a-hpc");

	box = gtk_hbox_new(FALSE, 10);
	dds_mode_lbl = gtk_label_new("DDS Mode:");
	dds_mode = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
	if (no_buffer_support == false)
		gtk_combo_box_text_prepend_text(dds_mode, "DAC Buffer Output");
	gtk_combo_box_text_prepend_text(dds_mode, "Independent I/Q Control");
	gtk_combo_box_text_prepend_text(dds_mode, "Two CW Tones");
	gtk_combo_box_text_prepend_text(dds_mode, "One CW Tone");
	gtk_combo_box_text_prepend_text(dds_mode, "Disable");
	gtk_combo_box_set_active(GTK_COMBO_BOX(dds_mode), 0);

	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(dds_mode_lbl), FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(dds_mode), FALSE, TRUE, 0);

	gtk_combo_box_set_active(GTK_COMBO_BOX(dds_mode), DDS_ONE_TONE);

	tx->dds_mode_widget = GTK_WIDGET(dds_mode);

	gtk_widget_show(box);

	return box;
}

static GtkWidget *gui_tone_create(struct dds_tone *tone)
{
	GtkWidget *tone_frm;
	GtkWidget *tone_align;
	GtkWidget *tone_table;
	bool combobox_scales;
	char tone_label[16];

	combobox_scales = tone->parent->parent->parent->parent->scale_available_mode;
	snprintf(tone_label, sizeof(tone_label), "<b>Tone %u</b>", tone->number);

	tone_frm = frame_with_table_create(tone_label, 3, 2);
	tone_align = gtk_bin_get_child(GTK_BIN(tone_frm));
	tone_table = gtk_bin_get_child(GTK_BIN(tone_align));
	tone->frame = tone_frm;

	gtk_frame_set_shadow_type(GTK_FRAME(tone_frm), GTK_SHADOW_NONE);

	GtkWidget *freq, *scale, *phase;

	freq = gtk_label_new("Frequency(MHz):");
	phase = gtk_label_new("Phase(degrees):");

	tone->freq = spin_button_create(0.0, 100.0, 1.0, FREQUENCY_SPIN_DIGITS);
	tone->phase = spin_button_create(0.0, 360.0, 1.0, PHASE_SPIN_DIGITS);

	gtk_misc_set_alignment(GTK_MISC(freq), 0.0, 0.5);
	gtk_misc_set_alignment(GTK_MISC(phase), 0.0, 0.5);

	if (combobox_scales) {
		scale = gtk_label_new("Scale:");
		tone->scale = gtk_combo_box_text_new();
	} else {
		scale = gtk_label_new("Scale(dBFS):");
		tone->scale = spin_button_create(-91.0, 0.0, 1.0, SCALE_SPIN_DIGITS);
		gtk_misc_set_alignment(GTK_MISC(scale), 0.0, 0.5);
		gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(tone->scale), FALSE);
	}

	gtk_table_attach(GTK_TABLE(tone_table), freq,
			0, 1, 0, 1, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach(GTK_TABLE(tone_table), scale,
			0, 1, 1, 2, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach(GTK_TABLE(tone_table), phase,
			0, 1, 2, 3, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach(GTK_TABLE(tone_table), tone->freq,
			1, 2, 0, 1, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach(GTK_TABLE(tone_table), tone->scale,
			1, 2, 1, 2, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach(GTK_TABLE(tone_table), tone->phase,
			1, 2, 2, 3, GTK_FILL, GTK_FILL, 0, 0);

	gtk_widget_show(tone_frm);

	return tone_frm;
}

static GtkWidget *gui_channel_create(struct dds_channel *ch)
{
	GtkWidget *channel_frm;
	GtkWidget *channel_align;
	GtkWidget *channel_table;
	char channel_label[32];

	snprintf(channel_label, sizeof(channel_label), "<b>Channel %c</b>", ch->type);

	channel_frm = frame_with_table_create(channel_label, 1, 2);
	channel_align = gtk_bin_get_child(GTK_BIN(channel_frm));
	channel_table = gtk_bin_get_child(GTK_BIN(channel_align));
	ch->frame = channel_frm;

	gtk_frame_set_shadow_type(GTK_FRAME(channel_frm), GTK_SHADOW_NONE);

	gtk_table_attach(GTK_TABLE(channel_table),
			gui_tone_create(&ch->t1),
			0, 1, 0, 1, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach(GTK_TABLE(channel_table),
			gui_tone_create(&ch->t2),
			1, 2, 0, 1, GTK_FILL, GTK_FILL, 0, 0);

	gtk_widget_show(channel_frm);

	return channel_frm;
}

static GtkWidget *gui_tx_create(struct dds_tx *tx)
{
	GtkWidget *txmodule_frm;
	GtkWidget *txmodule_align;
	GtkWidget *txmodule_table;
	char txmodule_label[16];
	unsigned int dac_index = (tx->parent->index - 1) * (tx->parent->tones_count / 4);

	snprintf(txmodule_label, sizeof(txmodule_label), "<b>TX %u</b>", dac_index + tx->index);

	txmodule_frm = frame_with_table_create(txmodule_label, 3, 1);
	txmodule_align = gtk_bin_get_child(GTK_BIN(txmodule_frm));
	txmodule_table = gtk_bin_get_child(GTK_BIN(txmodule_align));
	tx->frame = txmodule_frm;

	gtk_table_attach(GTK_TABLE(txmodule_table),
			gui_dds_mode_chooser_create(tx),
			0, 1, 0, 1, GTK_FILL, GTK_FILL, 0, 0);

	gtk_table_attach(GTK_TABLE(txmodule_table),
			gui_channel_create(&tx->ch_i),
			0, 1, 1, 2, GTK_FILL, GTK_FILL, 0, 0);
	if (tx->ch_q.type != CHAR_MAX)
		gtk_table_attach(GTK_TABLE(txmodule_table),
				gui_channel_create(&tx->ch_q),
				0, 1, 2, 3, GTK_FILL, GTK_FILL, 0, 0);

	gtk_widget_show(txmodule_frm);

	return txmodule_frm;
}

static GtkWidget *gui_dac_create(struct dds_dac *ddac)
{
	GtkWidget *dac_frm;
	GtkWidget *dac_align;
	GtkWidget *dac_table;
	gchar *frm_title;
	guint i;

	if (!ddac->iio_dac)
		return NULL;

	frm_title = g_strdup_printf("<b>%s</b>", ddac->name);
	dac_frm = frame_with_table_create(frm_title, 1, 1);
	dac_align = gtk_bin_get_child(GTK_BIN(dac_frm));
	dac_table = gtk_bin_get_child(GTK_BIN(dac_align));

	gtk_alignment_set_padding(GTK_ALIGNMENT(dac_align), 5, 5, 5, 5);

	for (i = 0; i < ddac->tx_count; i++)
		gtk_table_attach(GTK_TABLE(dac_table), gui_tx_create(&ddac->txs[i]),
			0 + i, 1 + i, 1, 2, GTK_FILL, GTK_FILL, 0, 0);

	ddac->frame = dac_frm;
	gtk_widget_show(dac_frm);

	g_free(frm_title);

	return dac_frm;
}

static GtkWidget *gui_dac_channels_tree_create(struct dac_buffer *d_buffer)
{
	GtkTreeStore *treestore;
	GtkWidget *treeview;
	GtkWidget *scrolled_window;

	treestore = gtk_tree_store_new(3, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_INT);
	treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(treestore));
	scrolled_window = gtk_scrolled_window_new(FALSE, FALSE);

	gtk_widget_set_size_request(treeview, -1, 110);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeview), FALSE);
	gtk_container_add(GTK_CONTAINER(scrolled_window), treeview);

	GtkTreeViewColumn *col;
	GtkCellRenderer *renderer_name;
	GtkCellRenderer *renderer_ch_toggle;

	col = gtk_tree_view_column_new();
	gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col);
	renderer_name = gtk_cell_renderer_text_new();
	renderer_ch_toggle = gtk_cell_renderer_toggle_new();

	gtk_tree_view_column_pack_end(col, renderer_name, FALSE);
	gtk_tree_view_column_pack_end(col, renderer_ch_toggle, FALSE);

	gtk_tree_view_column_set_attributes(col, renderer_name,
					"text", TX_CHANNEL_NAME,
					NULL);
	gtk_tree_view_column_set_attributes(col, renderer_ch_toggle,
					"active", TX_CHANNEL_ACTIVE,
					NULL);

	struct iio_device *dac = d_buffer->dac_with_scanelems;
	GtkTreeIter iter;
	unsigned int i;

	for (i = 0; i < iio_device_get_channels_count(dac); i++) {
		struct iio_channel *ch = iio_device_get_channel(dac, i);

		if (!iio_channel_is_scan_element(ch))
			continue;

		gtk_tree_store_append(treestore, &iter, NULL);
		gtk_tree_store_set(treestore, &iter,
				TX_CHANNEL_NAME, iio_channel_get_id(ch),
				TX_CHANNEL_ACTIVE, iio_channel_is_enabled(ch),
				TX_CHANNEL_REF_INDEX, i, -1);
	}

	gtk_widget_show(scrolled_window);

	g_signal_connect(renderer_ch_toggle, "toggled",
			G_CALLBACK(tx_channel_toggled), d_buffer);

	return scrolled_window;
}

static GtkWidget *gui_dac_buffer_create(struct dac_buffer *d_buffer)
{
	GtkWidget *dacbuf_frame;
	GtkWidget *dacbuf_align;
	GtkWidget *dacbuf_table;
	GtkWidget *fchooser_frame;
	GtkWidget *fchooser_btn;
	GtkWidget *fileload_btn;
	GtkWidget *load_status_txt;
	GtkWidget *scale;
	GtkWidget *tx_channels_frame;
	GtkTextBuffer *load_status_tb;

	dacbuf_frame = frame_with_table_create("<b>DAC Buffer Settings</b>", 2, 1);
	dacbuf_align = gtk_bin_get_child(GTK_BIN(dacbuf_frame));
	dacbuf_table = gtk_bin_get_child(GTK_BIN(dacbuf_align));
	fchooser_btn = gtk_file_chooser_button_new("Select a File",
			GTK_FILE_CHOOSER_ACTION_OPEN);
	fileload_btn = gtk_button_new_with_label("Load");
	load_status_tb = gtk_text_buffer_new(NULL);
	load_status_txt = gtk_text_view_new_with_buffer(load_status_tb);

	gtk_alignment_set_padding(GTK_ALIGNMENT(dacbuf_align), 5, 5, 5, 5);

	fchooser_frame = frame_with_table_create("<b>File Selection</b>", 3, 2);
	tx_channels_frame = frame_with_table_create("<b>DAC Channels</b>", 1, 1);

	gtk_text_view_set_editable(GTK_TEXT_VIEW(load_status_txt), false);

	GtkWidget *align, *table;

	d_buffer->scale = spin_button_create(-91.0, 0.0, 1.0, 2);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(d_buffer->scale), 0);
	scale = gtk_label_new("Scale(dBFS):");
	gtk_misc_set_alignment(GTK_MISC(scale), 0.0, 0.5);
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(d_buffer->scale), FALSE);

	align = gtk_bin_get_child(GTK_BIN(fchooser_frame));
	table = gtk_bin_get_child(GTK_BIN(align));

	gtk_frame_set_shadow_type(GTK_FRAME(fchooser_frame), GTK_SHADOW_NONE);
	gtk_alignment_set_padding(GTK_ALIGNMENT(align), 0, 0, 0, 0);
	gtk_table_attach(GTK_TABLE(table), fchooser_btn,
		0, 1, 0, 1, GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);
	gtk_table_attach(GTK_TABLE(table), fileload_btn,
		1, 2, 0, 1, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach(GTK_TABLE(table), load_status_txt,
		0, 2, 1, 2, GTK_FILL, GTK_FILL, 0, 0);

	gtk_table_attach(GTK_TABLE(table), d_buffer->scale,
			 1, 2, 2, 3, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach(GTK_TABLE(table), scale,
			 0, 2, 2, 3, GTK_FILL, GTK_FILL, 0, 0);

	align = gtk_bin_get_child(GTK_BIN(tx_channels_frame));
	table = gtk_bin_get_child(GTK_BIN(align));

	gtk_alignment_set_padding(GTK_ALIGNMENT(align), 0, 0, 0, 0);

	GtkWidget *channels_scrolled_view = gui_dac_channels_tree_create(d_buffer);

	gtk_table_attach(GTK_TABLE(table), channels_scrolled_view,
		0, 1, 0, 1, GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);

	gtk_table_attach(GTK_TABLE(dacbuf_table), fchooser_frame,
		0, 1, 0, 1, GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);
	gtk_table_attach(GTK_TABLE(dacbuf_table), tx_channels_frame,
		0, 1, 2, 3, GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);

	d_buffer->frame = dacbuf_frame;
	d_buffer->load_status_buf = load_status_tb;
	d_buffer->tx_channels_view = gtk_bin_get_child(GTK_BIN(channels_scrolled_view));
	d_buffer->buffer_fchooser_btn = fchooser_btn;

	g_signal_connect(fchooser_btn, "file-set",
		G_CALLBACK(dac_buffer_config_file_set_cb), d_buffer);
	g_signal_connect(fileload_btn, "clicked",
		G_CALLBACK(waveform_load_button_clicked_cb), d_buffer);
	g_signal_connect(d_buffer->scale, "output",
			 G_CALLBACK(scale_spin_button_output_cb), (void*) 1);

	gtk_widget_show(dacbuf_frame);

	return dacbuf_frame;
}

static void gui_manager_create(struct dac_data_manager *manager)
{
	GtkWidget *hbox;
	GtkWidget *vbox;

	if (!manager)
		return;

	hbox = gtk_hbox_new(FALSE, 0);
	vbox = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), gui_dac_create(&manager->dac1),
			FALSE, TRUE, 0);
	if (manager->dacs_count == 2)
		gtk_box_pack_start(GTK_BOX(vbox), gui_dac_create(&manager->dac2),
			FALSE, TRUE, 0);

	gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), gui_dac_buffer_create(&manager->dac_buffer_module),
			FALSE, TRUE, 0);

	manager->container = hbox;
	gtk_widget_show(hbox);
}

/* Uses the "TX*_I|Q_F* name convention to build a name for the channel at the given index.
 * E.g. TX1_I_F1 - 0 (the index)
 *      TX1_I_F2 - 1
 *      TX1_Q_F1 - 2
 *      TX1_Q_F2 - 3
 *      TX2_I_F1 - 4
 *      TX2_I_F2 - 5
 *      ...
 * Returns a newlly alocated char array.
 */
static char *build_default_channel_name_from_index(guint ch_index)
{
	guint tx_index = (ch_index / TX_NB_TONES) + 1; /* TX couting starts from 1 (not 0) */
	guint tone_index = (ch_index % 2) + 1; /* There are always 2 tones (I/Q). Count starts at 1 */
	const char i_q_type = (ch_index & 0x02) ? Q_CHANNEL : I_CHANNEL; /* First two indexes are I, next two are Q and so on*/

	return g_strdup_printf("TX%u_%c_F%u", tx_index, i_q_type, tone_index);
}

#define TONE_ID "altvoltage"
#define TONE_ID_SIZE (sizeof(TONE_ID) - 1)

/* Returns a newlly allocated tone name for the given channel in case of
 * success and NULL otherwise.
 */
static char * get_tone_name(struct iio_channel *ch)
{
	char *name;
	char tone_index;

	name = g_strdup( iio_channel_get_name(ch));

	/* If name convention "TX*_I|Q_F* is missing */
	if (name && strncmp(name, "TX", 2) != 0) {
		g_free(name);
		name = g_strdup(iio_channel_get_id(ch));
		if (name && !strncmp(name, TONE_ID, TONE_ID_SIZE)) {
			tone_index = name[TONE_ID_SIZE];
			if (tone_index && g_ascii_isdigit(tone_index))
				name = build_default_channel_name_from_index(tone_index - '0');
			else
				name = NULL;
		} else {
			name = NULL;
		}
	}

	return name;
}

static unsigned get_iio_tones_count(struct iio_device *dev)
{
	unsigned int i, count;

	for (i = 0, count = 0; i < iio_device_get_channels_count(dev); i++) {
		struct iio_channel *chn = iio_device_get_channel(dev, i);
		char *name = get_tone_name(chn);

		if (name && strncmp(name, "TX", 2) == 0)
			count++;

		g_free(name);
	}

	return count;
}

static int dac_channels_assign(struct dds_dac *ddac)
{
	struct dac_data_manager *manager;
	struct iio_device *dac = ddac->iio_dac;
	char *ch_name;
	unsigned int i, processed_ch = 0;

	if (!dac)
		return 0;

	manager = ddac->parent;

	for (i = 0; i < iio_device_get_channels_count(dac); i++) {
		struct iio_channel *chn = iio_device_get_channel(dac, i);
		ch_name = get_tone_name(chn);

		if (!ch_name || strlen(ch_name) == 0)
			goto err;

		int tx_index;
		char ch_type;
		int tone_index;

		char *s;

		if (!(s = strstr(ch_name, "TX")))
			goto err;
		tx_index = s[2] - '0';

		if ((s = strstr(ch_name, "_I_")))
			ch_type = I_CHANNEL;
		else if ((s = strstr(ch_name, "_Q_")))
			ch_type = Q_CHANNEL;
		else
			goto err;
		if (!(s = strstr(ch_name, "_F")))
			goto err;
		tone_index = s[2] - '0';

		struct dds_tx *tx = &ddac->txs[tx_index - 1]; /* Index extracted from name starts from 1 */

		struct dds_tone *matching_tone = NULL;
		if (ch_type == I_CHANNEL) {
			if (tone_index == 1)
				matching_tone = &tx->ch_i.t1;
			else if (tone_index == 2)
				matching_tone = &tx->ch_i.t2;
		} else if (ch_type == Q_CHANNEL) {
			if (tone_index == 1)
				matching_tone = &tx->ch_q.t1;
			else if (tone_index == 2)
				matching_tone = &tx->ch_q.t2;
		}

		if (!matching_tone)
			goto err;

		g_free(ch_name);

		matching_tone->iio_dac = dac;
		matching_tone->iio_ch = chn;
		manager->dds_tones = g_slist_prepend(manager->dds_tones, matching_tone);

		processed_ch++;

		continue;
err:
		if (ch_name)
			g_free(ch_name);
	}

	if (processed_ch != ddac->tones_count)
		return -1;

	return 0;
}

static int manager_channels_assign(struct dac_data_manager *manager)
{
	int ret;

	ret = dac_channels_assign(&manager->dac1);
	if (ret < 0)
		return ret;
	ret = dac_channels_assign(&manager->dac2);
	if (ret < 0)
		return ret;

	return 0;
}

static void save_widget_value(GtkWidget *widget, struct iio_widget *iio_w)
{
	iio_w->save(iio_w);
}

static void save_scale_widget_value(void *data)
{
	struct dds_tone *tone = data;
	struct dds_channel *dds_ch = tone->parent;
	struct iio_widget *scale_w = &tone->iio_scale;
	struct iio_widget *scale_pair_w = (tone->number == 1) ? &dds_ch->t2.iio_scale : &dds_ch->t1.iio_scale;
	double old_val, val1, val2;

	val1 = db_full_scale_convert(gtk_spin_button_get_value(GTK_SPIN_BUTTON(scale_w->widget)), false);
	iio_channel_attr_read_double(scale_w->chn, scale_w->attr_name, &old_val);
	iio_channel_attr_read_double(scale_pair_w->chn, scale_pair_w->attr_name, &val2);

	if (val1 + val2 > 1)
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(scale_w->widget), db_full_scale_convert(old_val, true));

	scale_w->save(scale_w);
}

static void dds_scale_set_value(GtkWidget *scale, gdouble value)
{
	if (GTK_IS_COMBO_BOX_TEXT(scale)) {
		gtk_combo_box_set_active(GTK_COMBO_BOX(scale), (gint)value);
	} else if (GTK_IS_SPIN_BUTTON(scale)){
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(scale), value);
	}
}

static double dds_scale_get_value(GtkWidget *scale)
{
	if (GTK_IS_COMBO_BOX_TEXT(scale)) {
		return (gint)gtk_combo_box_get_active(GTK_COMBO_BOX(scale));
	} else if (GTK_IS_SPIN_BUTTON(scale)) {
		return gtk_spin_button_get_value(GTK_SPIN_BUTTON(scale));
	}

	return 0;
}

static void dds_locked_phase_cb(GtkToggleButton *btn, struct dds_tx *tx)
{
	struct dds_tone **tones = tx->dds_tones;

	if (tx->parent->tones_count == 2) /* No I-Q available */
		return;

	gdouble phase1 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(tones[TX_T1_I]->phase));
	gdouble phase2 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(tones[TX_T2_I]->phase));

	gdouble freq1 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(tones[TX_T1_I]->freq));
	gdouble freq2 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(tones[TX_T2_I]->freq));

	gdouble inc1, inc2;

	if (freq1 >= 0)
		inc1 = 90.0;
	else
		inc1 = 270;

	if ((phase1 - inc1) < 0)
		phase1 += 360;

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(tx->dds_mode_widget))) {
		case DDS_ONE_TONE:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(tones[TX_T1_I + 2]->phase), phase1 - inc1);
			break;
		case DDS_TWO_TONE:
			if (freq2 >= 0)
				inc2 = 90;
			else
				inc2 = 270;
			if ((phase2 - inc2) < 0)
				phase2 += 360;

			gtk_spin_button_set_value(GTK_SPIN_BUTTON(tones[TX_T1_I + 2]->phase), phase1 - inc1);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(tones[TX_T2_I + 2]->phase), phase2 - inc2);
			break;
		default:
			printf("%s: error\n", __func__);
			break;
	}
}

static void dds_locked_freq_cb(GtkToggleButton *btn, struct dds_tx *tx)
{
	struct dds_tone **tones = tx->dds_tones;

		if (tx->parent->tones_count == 2) /* No I-Q available */
			return;

	gdouble freq1 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(tones[TX_T1_I]->freq));
	gdouble freq2 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(tones[TX_T2_I]->freq));

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(tx->dds_mode_widget))) {
		case DDS_ONE_TONE:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(tones[TX_T1_I + 2]->freq), freq1);
			break;
		case DDS_TWO_TONE:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(tones[TX_T1_I + 2]->freq), freq1);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(tones[TX_T2_I + 2]->freq), freq2);
			break;
		default:
			printf("%s: error : %i\n", __func__,
					gtk_combo_box_get_active(GTK_COMBO_BOX(tx->dds_mode_widget)));
			break;
	}

	dds_locked_phase_cb(NULL, tx);
}

static void dds_locked_scale_cb(GtkWidget *scale, struct dds_tx *tx)
{
	struct dds_tone **tones = tx->dds_tones;

	if (tx->parent->tones_count == 2) /* No I-Q available */
		return;

	gdouble scale1 = dds_scale_get_value(tones[TX_T1_I]->scale);
	gdouble scale2 = dds_scale_get_value(tones[TX_T2_I]->scale);

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(tx->dds_mode_widget))) {
		case DDS_ONE_TONE:
			dds_scale_set_value(tones[TX_T1_I + 2]->scale, scale1);
			break;
		case DDS_TWO_TONE:
			dds_scale_set_value(tones[TX_T1_I + 2]->scale, scale1);
			dds_scale_set_value(tones[TX_T2_I + 2]->scale, scale2);
			break;
		default:
			break;
	}
}

static void manage_dds_mode (GtkComboBox *box, struct dds_tx *tx)
{
	struct dac_data_manager *manager;
	guint active;
	double min_scale;
	bool scale_available_mode;
	bool q_tone_exists;
	unsigned tones_count;
	unsigned i;

	if (!box)
		return;

	manager = tx->parent->parent;
	tones_count = manager->dac1.tones_count;
	q_tone_exists = (tones_count > 2);
	min_scale = manager->lowest_scale_point;
	scale_available_mode = manager->scale_available_mode;

	active = gtk_combo_box_get_active(box);
	if (active != DDS_BUFFER) {
		struct dds_tx *txs = manager->dac1.txs;
		for (i = 0; i < manager->dac1.tx_count; i++) {
			GtkWidget *widget = txs[i].dds_mode_widget;
			if (gtk_combo_box_get_active(GTK_COMBO_BOX(widget)) == DDS_BUFFER) {
				gtk_combo_box_set_active(GTK_COMBO_BOX(widget), active);
				manage_dds_mode(GTK_COMBO_BOX(widget), &txs[i]);
			}
		}
		txs = manager->dac2.txs;
		for (i = 0; i < manager->dac2.tx_count; i++) {
			GtkWidget *widget = txs[i].dds_mode_widget;
			if (gtk_combo_box_get_active(GTK_COMBO_BOX(widget)) == DDS_BUFFER) {
				gtk_combo_box_set_active(GTK_COMBO_BOX(widget), active);
				manage_dds_mode(GTK_COMBO_BOX(widget), &txs[i]);
			}
		}
	}

	struct dds_tone **tones = tx->dds_tones;

	switch (active) {
	case DDS_DISABLED:
		for (i = TX_T1_I; i <= TX_T2_Q; i++) {
			if (i >= tones_count)
				break;
			struct dds_tone *tone = tones[i];
			GtkWidget *scale_w = tone->scale;

			if (dds_scale_get_value(scale_w) != min_scale) {
				tone->scale_state = dds_scale_get_value(scale_w);
				dds_scale_set_value(scale_w, min_scale);
			}
		}

		bool start_dds = false;
		GSList *node;

		for (node = manager->dds_tones; node; node = g_slist_next(node)) {
			struct dds_tone *tn = node->data;
			if (dds_scale_get_value(tn->scale) != min_scale) {
				start_dds = true;
				break;
			}
		}

		if (!manager->dds_activated && manager->dds_buffer) {
			iio_buffer_destroy(manager->dds_buffer);
			manager->dds_buffer = NULL;
		}
		manager->dds_disabled = true;
		enable_dds(manager, start_dds);

		gtk_widget_hide(tx->ch_i.frame);
		if (q_tone_exists)
			gtk_widget_hide(tx->ch_q.frame);
		gtk_widget_hide(manager->dac_buffer_module.frame);
		break;
	case DDS_ONE_TONE:
		enable_dds(manager, true);
		gtk_label_set_markup(GTK_LABEL(gtk_frame_get_label_widget(GTK_FRAME(tx->ch_i.frame))),
				"<b>Single Tone</b>");
		gtk_widget_show_all(tx->ch_i.frame);
		gtk_widget_hide(tx->ch_i.t2.frame);
		if (q_tone_exists)
			gtk_widget_hide(tx->ch_q.frame);
		gtk_widget_hide(manager->dac_buffer_module.frame);

		if (dds_scale_get_value(tones[TX_T1_I]->scale) == min_scale) {
			dds_scale_set_value(tones[TX_T1_I]->scale, tones[TX_T1_I]->scale_state);
			if (q_tone_exists)
				dds_scale_set_value(tones[TX_T1_Q]->scale, tones[TX_T1_Q]->scale_state);
		}

		if (dds_scale_get_value(tones[TX_T2_I]->scale) != min_scale) {
			tones[TX_T2_I]->scale_state = dds_scale_get_value(tones[TX_T2_I]->scale);
			dds_scale_set_value(tones[TX_T2_I]->scale, min_scale);
			if (q_tone_exists) {
				tones[TX_T2_Q]->scale_state = dds_scale_get_value(tones[TX_T2_Q]->scale);
				dds_scale_set_value(tones[TX_T2_Q]->scale, min_scale);
			}
		}

		/* Connect the widgets that are showing */
		if (!tones[TX_T1_I]->dds_scale_hid) {
			if (scale_available_mode) {
				tones[TX_T1_I]->dds_scale_hid = g_signal_connect(tones[TX_T1_I]->scale, IIO_COMBO_SIGNAL,
						G_CALLBACK(dds_locked_scale_cb), tx);
			} else {
				tones[TX_T1_I]->dds_scale_hid = g_signal_connect(tones[TX_T1_I]->scale, IIO_SPIN_SIGNAL,
						G_CALLBACK(dds_locked_scale_cb), tx);
			}
		}

		if (!tones[TX_T1_I]->dds_freq_hid) {
			tones[TX_T1_I]->dds_freq_hid = g_signal_connect(tones[TX_T1_I]->freq, IIO_SPIN_SIGNAL,
						G_CALLBACK(dds_locked_freq_cb), tx);
		}
		if (!tones[TX_T1_I]->dds_phase_hid) {
			tones[TX_T1_I]->dds_phase_hid = g_signal_connect(tones[TX_T1_I]->phase, IIO_SPIN_SIGNAL,
						G_CALLBACK(dds_locked_phase_cb), tx);
		}

		/* Disconnect the rest */
		if (tones[TX_T2_I]->dds_scale_hid) {
			g_signal_handler_disconnect(tones[TX_T2_I]->scale, tones[TX_T2_I]->dds_scale_hid);
			tones[TX_T2_I]->dds_scale_hid = 0;
		}
		if (tones[TX_T2_I]->dds_freq_hid) {
			g_signal_handler_disconnect(tones[TX_T2_I]->freq, tones[TX_T2_I]->dds_freq_hid);
			tones[TX_T2_I]->dds_freq_hid = 0;
		}
		if (tones[TX_T2_I]->dds_phase_hid) {
			g_signal_handler_disconnect(tones[TX_T2_I]->phase, tones[TX_T2_I]->dds_phase_hid);
			tones[TX_T2_I]->dds_phase_hid = 0;
		}

		/* Force sync */
		dds_locked_scale_cb(NULL, tx);
		dds_locked_freq_cb(NULL, tx);
		dds_locked_phase_cb(NULL, tx);
		break;
	case DDS_TWO_TONE:
		enable_dds(manager, true);
		gtk_label_set_markup(GTK_LABEL(gtk_frame_get_label_widget(GTK_FRAME(tx->ch_i.frame))),
				"<b>Two Tones</b>");
		gtk_widget_show_all(tx->ch_i.frame);
		if (q_tone_exists)
			gtk_widget_hide(tx->ch_q.frame);
		gtk_widget_hide(manager->dac_buffer_module.frame);

		for (i = TX_T1_I; i <= TX_T2_Q; i++) {
			if (i >= tones_count)
				break;
			if (dds_scale_get_value(tones[i]->scale) == min_scale) {
				dds_scale_set_value(tones[i]->scale, tones[i]->scale_state);
			}
		}

		for (i = TX_T1_I; i <= TX_T2_Q; i++) {
			if (i >= tones_count)
				break;
			if (!tones[i]->dds_scale_hid) {
				if (scale_available_mode) {
					tones[i]->dds_scale_hid = g_signal_connect(tones[i]->scale, IIO_COMBO_SIGNAL,
							G_CALLBACK(dds_locked_scale_cb), tx);
				} else {
					tones[i]->dds_scale_hid = g_signal_connect(tones[i]->scale, IIO_SPIN_SIGNAL,
							G_CALLBACK(dds_locked_scale_cb), tx);
				}
			}
			if (!tones[i]->dds_freq_hid)
				tones[i]->dds_freq_hid = g_signal_connect(tones[i]->freq , IIO_SPIN_SIGNAL,
						G_CALLBACK(dds_locked_freq_cb), tx);
			if (!tones[i]->dds_phase_hid)
				tones[i]->dds_phase_hid = g_signal_connect(tones[i]->phase, IIO_SPIN_SIGNAL,
						G_CALLBACK(dds_locked_phase_cb), tx);
		}

		/* Force sync */
		dds_locked_scale_cb(NULL, tx);
		dds_locked_freq_cb(NULL, tx);
		dds_locked_phase_cb(NULL, tx);

		break;
	case DDS_INDEPDENT:
		/* Independent/Individual control */
		enable_dds(manager, true);
		gtk_label_set_markup(GTK_LABEL(gtk_frame_get_label_widget(GTK_FRAME(tx->ch_i.frame))),
				"<b>Channel I</b>");
		gtk_widget_show_all(tx->ch_i.frame);
		if (q_tone_exists)
			gtk_widget_show_all(tx->ch_q.frame);
		gtk_widget_hide(manager->dac_buffer_module.frame);

		for (i = TX_T1_I; i <= TX_T2_Q; i++) {
			if (i >= tones_count)
				break;
			if (dds_scale_get_value(tones[i]->scale) == min_scale)
				 dds_scale_set_value(tones[i]->scale, tones[i]->scale_state);

			if (tones[i]->dds_scale_hid) {
				g_signal_handler_disconnect(tones[i]->scale, tones[i]->dds_scale_hid);
				tones[i]->dds_scale_hid = 0;
			}
			if (tones[i]->dds_freq_hid) {
				g_signal_handler_disconnect(tones[i]->freq, tones[i]->dds_freq_hid);
				tones[i]->dds_freq_hid = 0;
			}
			if (tones[i]->dds_phase_hid) {
				g_signal_handler_disconnect(tones[i]->phase, tones[i]->dds_phase_hid);
				tones[i]->dds_phase_hid = 0;
			}
		}
		break;
	case DDS_BUFFER:
		if ((manager->dds_activated || manager->dds_disabled) && manager->dac_buffer_module.dac_buf_filename) {
			manager->dds_disabled = false;
		}

		gtk_widget_hide(tx->ch_i.frame);
		if (q_tone_exists)
			gtk_widget_hide(tx->ch_q.frame);
		gtk_widget_show(manager->dac_buffer_module.frame);

		for (i = 0; i < manager->dac1.tx_count; i++)
			gtk_combo_box_set_active(GTK_COMBO_BOX(manager->dac1.txs[i].dds_mode_widget), DDS_BUFFER);
		for (i = 0; i < manager->dac2.tx_count; i++)
			gtk_combo_box_set_active(GTK_COMBO_BOX(manager->dac2.txs[i].dds_mode_widget), DDS_BUFFER);

		break;
	default:
		break;
	}
}

static void tone_setup(struct dds_tone *tone)
{
	bool combobox_scales = tone->parent->parent->parent->parent->scale_available_mode;

	/* Bind the IIO Channel attributes to the GUI widgets */
	iio_spin_button_s64_init(&tone->iio_freq,
			tone->iio_dac, tone->iio_ch, "frequency", tone->freq, &abs_mhz_scale);
	iio_spin_button_add_progress(&tone->iio_freq);

	if (combobox_scales) {
		iio_combo_box_init(&tone->iio_scale, tone->iio_dac, tone->iio_ch, "scale",
			"scale_available", tone->scale, compare_gain);
	} else {
		iio_spin_button_init(&tone->iio_scale,
			tone->iio_dac, tone->iio_ch, "scale", tone->scale, NULL);
		iio_spin_button_set_convert_function(&tone->iio_scale, db_full_scale_convert);
		iio_spin_button_add_progress(&tone->iio_scale);
		iio_spin_button_set_on_complete_function(&tone->iio_scale,
				save_scale_widget_value, tone);
		iio_spin_button_skip_save_on_complete(&tone->iio_scale, TRUE);
	}

	iio_spin_button_init(&tone->iio_phase,
			tone->iio_dac, tone->iio_ch, "phase", tone->phase, &khz_scale);
	iio_spin_button_add_progress(&tone->iio_phase);

	/* Signals connect */
	iio_spin_button_progress_activate(&tone->iio_freq);
	iio_spin_button_progress_activate(&tone->iio_phase);
	if (combobox_scales) {
		g_signal_connect(tone->scale, "changed",
			G_CALLBACK(save_widget_value), &tone->iio_scale);
	} else {
		g_signal_connect(tone->scale, "output",
			G_CALLBACK(scale_spin_button_output_cb), NULL);
		iio_spin_button_progress_activate(&tone->iio_scale);
	}

}

static void manager_iio_setup(struct dac_data_manager *manager)
{
	GSList *node;
	guint i;

	for (node = manager->dds_tones; node; node = g_slist_next(node))
		tone_setup(node->data);

	for (i = 0; i < manager->dac1.tx_count; i++)
		g_signal_connect(manager->dac1.txs[i].dds_mode_widget, "changed", G_CALLBACK(manage_dds_mode),
			&manager->dac1.txs[i]);
	for (i = 0; i < manager->dac2.tx_count; i++)
		g_signal_connect(manager->dac2.txs[i].dds_mode_widget, "changed", G_CALLBACK(manage_dds_mode),
			&manager->dac2.txs[i]);

	for (node = manager->dds_tones; node; node = g_slist_next(node)) {
		struct dds_tone *tn = node->data;

		tn->scale_state = dds_scale_get_value(tn->scale);
	}

	struct dds_tone *tone = &manager->dac1.txs[0].ch_i.t1;

	if (manager->scale_available_mode) {
		GtkWidget *scale_cmb = tone->scale;
		gint active;

		active = (gint)tone->scale_state;
		while (dds_scale_get_value(scale_cmb) >= 0) {
			active++;
			dds_scale_set_value(scale_cmb, active);
		}
		manager->lowest_scale_point = active - 1;
		dds_scale_set_value(scale_cmb, tone->scale_state);

	} else {
		GtkSpinButton *scale_btn = GTK_SPIN_BUTTON(manager->dac1.txs[0].ch_i.t1.scale);

		manager->lowest_scale_point = gtk_adjustment_get_lower(gtk_spin_button_get_adjustment(scale_btn));
	}
}

static void dds_tx_init(struct dds_dac *ddac, struct dds_tx *tx, unsigned dds_index)
{
	tx->index = dds_index;
	tx->ch_i.type = I_CHANNEL;
	tx->ch_i.t1.number = 1;
	tx->ch_i.t2.number = 2;
	tx->ch_q.type = Q_CHANNEL;
	tx->ch_q.t1.number = 1;
	tx->ch_q.t2.number = 2;

	tx->parent = ddac;
	tx->ch_i.parent = tx;
	tx->ch_q.parent = tx;
	tx->ch_i.t1.parent = &tx->ch_i;
	tx->ch_i.t2.parent = &tx->ch_i;
	tx->ch_q.t1.parent = &tx->ch_q;
	tx->ch_q.t2.parent = &tx->ch_q;

	tx->dds_tones[0] = &tx->ch_i.t1;
	tx->dds_tones[1] = &tx->ch_i.t2;
	tx->dds_tones[2] = &tx->ch_q.t1;
	tx->dds_tones[3] = &tx->ch_q.t2;

	ddac->tx_count++;
}

static void dds_non_iq_tx_init(struct dds_dac *ddac, struct dds_tx *tx, unsigned dds_index)
{
	tx->index = dds_index;
	tx->ch_i.type = I_CHANNEL;
	tx->ch_i.t1.number = 1;
	tx->ch_i.t2.number = 2;
	tx->ch_q.type = CHAR_MAX;
	tx->ch_q.t1.number = 0;
	tx->ch_q.t2.number = 0;

	tx->parent = ddac;
	tx->ch_i.parent = tx;
	tx->ch_q.parent = NULL;
	tx->ch_i.t1.parent = &tx->ch_i;
	tx->ch_i.t2.parent = &tx->ch_i;
	tx->ch_q.t1.parent = NULL;
	tx->ch_q.t2.parent = NULL;

	tx->dds_tones[0] = &tx->ch_i.t1;
	tx->dds_tones[1] = &tx->ch_i.t2;
	tx->dds_tones[2] = NULL;
	tx->dds_tones[3] = NULL;

	ddac->tx_count++;
}

static int dds_dac_init(struct dac_data_manager *manager,
	struct dds_dac *ddac, struct iio_device *iio_dac)
{
	int ret = 0;

	if (!iio_dac)
		return 0;

	ddac->parent = manager;
	ddac->iio_dac = iio_dac;
	ddac->name = iio_device_get_name(iio_dac);
	ddac->tones_count = get_iio_tones_count(iio_dac);

	guint tx_count = ddac->tones_count / TX_NB_TONES;
	guint extra_tones = ddac->tones_count % TX_NB_TONES;
	if (tx_count == 0) {
		/* Some devices don't have the I-Q concept. One use case is: AD9739A eval board */
		if (extra_tones == 2) {
			ddac->txs = calloc(1, sizeof(struct dds_tx));
			dds_non_iq_tx_init(ddac, &ddac->txs[0], 1);
		} else {
			fprintf(stderr, "DacDataManager can't handle a device"
			"with %u number of tones\n", ddac->tones_count);
			return -1;
		}
	} else {
		ddac->txs = calloc(tx_count, sizeof(struct dds_tx));
		guint tx = 0;
		for (; tx < tx_count; tx++) {
			dds_tx_init(ddac, &ddac->txs[tx], tx + 1);
		}
	}

	manager->dacs_count++;
	ddac->index = manager->dacs_count;

	return ret;
}

int device_scan_elements_count(struct iio_device *dev)
{
	unsigned int i;
	int count;

	for (i = 0, count = 0; i < iio_device_get_channels_count(dev); i++) {
		struct iio_channel *ch = iio_device_get_channel(dev, i);

		if (iio_channel_is_scan_element(ch))
			count++;
	}

	return count;
}

static void dac_buffer_init(struct dac_data_manager *manager, struct dac_buffer *d_buffer)
{
	int count;
	struct iio_device *dac_with_scanelems;

	dac_with_scanelems = manager->dac1.iio_dac;
	count = device_scan_elements_count(dac_with_scanelems);
	if (manager->dacs_count == 2 && count == 0) {
		dac_with_scanelems = manager->dac2.iio_dac;
		count = device_scan_elements_count(dac_with_scanelems);
	}

	d_buffer->parent = manager;
	d_buffer->scan_elements_count = count;
	d_buffer->dac_with_scanelems = dac_with_scanelems;
}

static int dac_manager_init(struct dac_data_manager *manager,
		struct iio_device *dac, struct iio_device *second_dac, struct iio_context *ctx)
{
	long long alignment;
	int ret = 0;

	ret = dds_dac_init(manager, &manager->dac1, dac);
	if (ret < 0)
		return ret;
	ret = dds_dac_init(manager, &manager->dac2, second_dac);
	if (ret < 0)
		return ret;

	dac_buffer_init(manager, &manager->dac_buffer_module);

	struct iio_channel *ch = iio_device_find_channel(dac, "altvoltage0", true);

	if (iio_channel_find_attr(ch, "scale_available"))
		manager->scale_available_mode = true;
	else
		manager->scale_available_mode = false;

	manager->is_local = strcmp(iio_context_get_name(ctx), "local") ? false : true;
	manager->ctx = ctx;

	if (iio_device_buffer_attr_read_longlong(manager->dac_buffer_module.dac_with_scanelems,
						 "length_align_bytes",
						 &alignment) == 0) {
		manager->alignment = alignment;
		manager->hw_reported_alignment = true;
	 } else {
		manager->alignment = 8;
		manager->hw_reported_alignment = false;
	}

	return ret;
}

struct dac_data_manager *dac_data_manager_new(struct iio_device *dac,
		struct iio_device *second_dac, struct iio_context *ctx)
{
	struct dac_data_manager *manager;
	int ret;

	manager = calloc(1, sizeof(struct dac_data_manager));
	if (!manager) {
		printf("Memory allocation of struct dac_data_manager failed\n");
		return NULL;
	}

	if (!ctx || !dac)
		goto init_error;

	ret = dac_manager_init(manager, dac, second_dac, ctx);
	if (ret < 0)
		goto init_error;

	ret = manager_channels_assign(manager);
	if (ret < 0)
		goto init_error;
	gui_manager_create(manager);
	manager_iio_setup(manager);

	return manager;

init_error:
	dac_data_manager_free(manager);
	return NULL;
}

void dac_data_manager_free(struct dac_data_manager *manager)
{
	if (manager) {
		free(manager->dac1.txs);
		free(manager->dac2.txs);
		if (manager->dds_buffer) {
			iio_buffer_destroy(manager->dds_buffer);
			manager->dds_buffer = NULL;
		}
		g_slist_free(manager->dds_tones);
		free(manager);
	}
}

static void freq_spin_range_update(struct dds_tone *tone, double tx_sample_rate)
{
	GtkAdjustment *adj;
	gdouble val;

	adj = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(tone->freq));
	val = gtk_adjustment_get_value(adj);
	if (fabs(val) > tx_sample_rate)
		gtk_adjustment_set_value(adj, tx_sample_rate);
	gtk_adjustment_set_lower(adj, -1 * tx_sample_rate);
	gtk_adjustment_set_upper(adj, tx_sample_rate);
}

void dac_data_manager_freq_widgets_range_update(struct dac_data_manager *manager, double tx_sample_rate)
{
	GSList *node;

	if (!manager)
		return;

	for (node = manager->dds_tones; node; node = g_slist_next(node))
		freq_spin_range_update(node->data, tx_sample_rate);

}

static void dds_tone_iio_widgets_update(struct dds_tone *tone)
{
	tone->iio_freq.update(&tone->iio_freq);
	tone->iio_scale.update(&tone->iio_scale);
	tone->iio_phase.update(&tone->iio_phase);
}

void dac_data_manager_update_iio_widgets(struct dac_data_manager *manager)
{
	GSList *node;
	unsigned i;

	if (!manager)
		return;

	for (node = manager->dds_tones; node; node = g_slist_next(node))
		dds_tone_iio_widgets_update(node->data);

	for (i = 0; i < manager->dac1.tx_count; i++)
		manage_dds_mode(GTK_COMBO_BOX(manager->dac1.txs[i].dds_mode_widget), &manager->dac1.txs[i]);
	for (i = 0; i < manager->dac2.tx_count; i++)
		manage_dds_mode(GTK_COMBO_BOX(manager->dac2.txs[i].dds_mode_widget), &manager->dac2.txs[i]);
}

int dac_data_manager_set_dds_mode(struct dac_data_manager *manager,
		const char *dac_name, unsigned tx_index, int mode)
{
	if (!manager || !dac_name || !tx_index)
		return -1;

	if (mode < DDS_DISABLED  || mode > DDS_BUFFER)
		return -1;

	GtkWidget *dds_mode_combobox;

	if (!strcmp(dac_name, iio_device_get_name(manager->dac1.iio_dac))) {
		if (tx_index > manager->dac1.tx_count)
			return -1;
		dds_mode_combobox = manager->dac1.txs[tx_index - 1].dds_mode_widget;
	} else if (manager->dacs_count == 2 &&
			!strcmp(dac_name, iio_device_get_name(manager->dac2.iio_dac))) {
		if (tx_index > manager->dac2.tx_count)
			return -1;
		dds_mode_combobox = manager->dac2.txs[tx_index - 1].dds_mode_widget;
	} else {
		return -1;
	}

	gtk_combo_box_set_active(GTK_COMBO_BOX(dds_mode_combobox), mode);

	return 0;
}

int  dac_data_manager_get_dds_mode(struct dac_data_manager *manager, const char *dac_name, unsigned tx_index)
{
	if (!manager || !dac_name || !tx_index)
		return 0;

	GtkWidget *dds_mode_combobox;

	if (!strcmp(dac_name, iio_device_get_name(manager->dac1.iio_dac))) {
		if (tx_index > manager->dac1.tx_count)
			return 0;
		dds_mode_combobox = manager->dac1.txs[tx_index - 1].dds_mode_widget;
	} else if (manager->dacs_count == 2 &&
			!strcmp(dac_name, iio_device_get_name(manager->dac2.iio_dac))) {
		if (tx_index > manager->dac2.tx_count)
			return 0;
		dds_mode_combobox = manager->dac2.txs[tx_index - 1].dds_mode_widget;
	} else {
		return 0;
	}

	return gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode_combobox));
}

void dac_data_manager_set_buffer_chooser_current_folder(struct dac_data_manager *manager, const char *path)
{
	if (!manager || !path)
		return;
	GtkWidget *fchooser = manager->dac_buffer_module.buffer_fchooser_btn;

	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(fchooser), path);
}

#define NULL_FILENAME "(null)"

void dac_data_manager_set_buffer_chooser_filename(struct dac_data_manager *manager, const char *filename)
{
	if (!manager || !filename)
		return;
	if (!strncmp(filename, NULL_FILENAME, strlen(NULL_FILENAME)))
		return;

	GtkWidget *fchooser = manager->dac_buffer_module.buffer_fchooser_btn;

	gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(fchooser), filename);
	g_signal_emit_by_name(fchooser, "file-set", NULL);
	waveform_load_button_clicked_cb(NULL, &manager->dac_buffer_module);
}

void dac_data_manager_set_buffer_size_alignment(struct dac_data_manager *manager, unsigned align)
{
	if (!manager)
		return;

	/*
	 * This is just a backup in case the alignment was not reported by the
	 * hardware itself. If it was reported by the hardware ignore this
	 * value.
	 */
	if (!manager->hw_reported_alignment)
		manager->alignment = align;
}

char *dac_data_manager_get_buffer_chooser_filename(struct dac_data_manager *manager)
{
	if (!manager)
		return NULL;

	return manager->dac_buffer_module.dac_buf_filename;
}

void dac_data_manager_set_tx_channel_state(struct dac_data_manager *manager,
		unsigned ch_index, bool state)
{
	if (!manager)
		return;

	GtkTreeIter iter;
	GtkTreeModel *model;
	gboolean next_iter;
	unsigned int index;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(manager->dac_buffer_module.tx_channels_view));
	if (!gtk_tree_model_get_iter_first(model, &iter))
		return;

	next_iter = true;
	while (next_iter) {
		gtk_tree_model_get(model, &iter, TX_CHANNEL_REF_INDEX, &index, -1);
		if (index == ch_index) {
			gtk_tree_store_set(GTK_TREE_STORE(model), &iter, TX_CHANNEL_ACTIVE, state, -1);
			break;
		}

		next_iter = gtk_tree_model_iter_next(model, &iter);
	}
}
bool dac_data_manager_get_tx_channel_state(struct dac_data_manager *manager, unsigned ch_index)
{
	if (!manager)
		return false;

	GtkTreeIter iter;
	GtkTreeModel *model;
	gboolean next_iter;
	unsigned int index;
	gboolean state;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(manager->dac_buffer_module.tx_channels_view));
	if (!gtk_tree_model_get_iter_first(model, &iter))
		return false;

	next_iter = true;
	while (next_iter) {
		gtk_tree_model_get(model, &iter, TX_CHANNEL_REF_INDEX, &index, -1);
		if (index == ch_index) {
			gtk_tree_model_get(model, &iter, TX_CHANNEL_ACTIVE, &state, -1);
			return state;
		}

		next_iter = gtk_tree_model_iter_next(model, &iter);
	}

	return false;
}

static struct dds_tone *dds_tone_find(struct dac_data_manager *manager,
		enum dds_tone_type tone)
{
	GSList *node;
	struct dds_tone *tn = NULL;
	unsigned int tone_type;

	for (node = manager->dds_tones; node; node = g_slist_next(node)) {
		tn = node->data;
		tone_type = 0;

		struct dds_channel *ch = tn->parent;
		struct dds_tx *tx = ch->parent;
		struct dds_dac *dac = tx->parent;

		tone_type += tn->number - 1;
		tone_type += (ch->type == I_CHANNEL) ? 0 : 2;
		tone_type += 4 * ((2 * (dac->index - 1)) + (tx->index - 1));

		if (tone_type == tone)
			break;
	}

	return tn;
}

GtkWidget *dac_data_manager_get_widget(struct dac_data_manager *manager,
		enum dds_tone_type tone, enum dds_widget_type type)
{
	if (!manager)
		return NULL;

	GtkWidget *widget = NULL;
	struct dds_tone *tn;

	tn = dds_tone_find(manager, tone);
	if (!tn)
		return NULL;

	switch(type) {
	case WIDGET_FREQUENCY:
		widget = tn->freq;
		break;
	case WIDGET_SCALE:
		widget = tn->scale;
		break;
	case WIDGET_PHASE:
		widget = tn->phase;
		break;
	}

	return widget;
}

GtkWidget *dac_data_manager_get_gui_container(struct dac_data_manager *manager)
{
	if (!manager)
		return NULL;

	return manager->container;
}

/* Returns a DDS tone from the given TX index (0, 1, ..),
 * tone index (1st or 2nd) and tone type (I or Q).
 */
unsigned dac_data_manager_dds_tone(unsigned tx_index,
	enum dds_tone_index tone_index, enum dds_tone_type tone_type)
{
	return ((tx_index * TX_NB_TONES) + (tone_index * CHANNEL_NB_TONES) + tone_type);
}
