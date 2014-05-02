/**
 * Copyright (C) 2012-2014 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 */

static short convert(double scale, float val)
{
	return (short) (val * scale);
}

static int analyse_wavefile(const char *file_name, char **buf, int *count, int tx)
{
	int ret, j, i = 0, size, rep;
	double max = 0.0, val[4], scale = 0.0;
	double i1, q1, i2, q2;
	char line[80];

	FILE *infile = fopen(file_name, "r");
	if (infile == NULL)
		return -3;

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
				ret = sscanf(line, "%lf%*[, \t]%lf%*[, \t]%lf%*[, \t]%lf",
						&val[0], &val[1], &val[2], &val[3]);

				if (!(ret == 4 || ret == 2)) {
					fclose(infile);
					return -2;
				}

				for (i = 0; i < ret; i++)
					if (fabs(val[i]) > max)
						max = fabs(val[i]);

				size += ((tx == 2) ? 8 : 4);


			}

			size *= rep;
			if (scale == 0.0)
				scale = 32767.0 / max;

			if (max > 32767.0)
				fprintf(stderr, "ERROR: DAC Waveform Samples > +/- 2047.0\n");

			*buf = malloc(size);
			if (*buf == NULL)
				return 0;

			unsigned long long *sample = *((unsigned long long **) buf);
			unsigned int *sample_32 = *((unsigned int **) buf);

			rewind(infile);

			if (fgets(line, 80, infile) != NULL) {
				if (strncmp(line, "TEXT", 4) == 0) {
					size = 0;
					i = 0;
					while (fgets(line, 80, infile)) {

						ret = sscanf(line, "%lf%*[, \t]%lf%*[, \t]%lf%*[, \t]%lf",
								&i1, &q1, &i2, &q2);
						for (j = 0; j < rep; j++) {
							if (ret == 4 && tx == 2) {
								sample[i++] = ((unsigned long long) convert(scale, q2) << 48) +
								    ((unsigned long long) convert(scale, i2) << 32) +
								    (convert(scale, q1) << 16) +
								    (convert(scale, i1) << 0);

								size += 8;
							}
							if (ret == 2 && tx == 2) {
								sample[i++] = ((unsigned long long) convert(scale, q1) << 48) +
								    ((unsigned long long) convert(scale, i1) << 32) +
								    (convert(scale, q1) << 16) +
								    (convert(scale, i1) << 0);

								size += 8;
							}
							if (tx == 1) {
								sample_32[i++] = (convert(scale, q1) << 16) +
									(convert(scale, i1) << 0);

								size += 4;
							}
						}
					}
				}
			}

			fclose(infile);
			*count = size;

		} else {
			/* Is it a MATLAB file?
			 * http://na-wiki.csc.kth.se/mediawiki/index.php/MatIO
			 */
		}
	} else {
		fclose(infile);
		*buf = NULL;
		return -1;
	}

	return 0;
}
