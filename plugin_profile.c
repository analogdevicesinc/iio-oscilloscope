#include "plugin_profile.h"

/* Internal Functions */
static int profile_elements_add(GSList **list, int elem_type,
		struct iio_device *dev, struct iio_channel *chn, const char *attribute)
{
	ProfileElement *pelement;
	const char *dev_name;
	const char *ch_name;
	gchar *ch_tmp = NULL;

	if (!attribute)
		return -1;

	pelement = malloc(sizeof(ProfileElement));
	if (!pelement)
		return -1;

	pelement->type = elem_type;
	pelement->device = dev;
	pelement->channel = chn;
	pelement->attribute = attribute;

	switch (elem_type) {
		case PROFILE_DEVICE_ELEMENT:
				if (dev)
					dev_name = iio_device_get_name(dev);
				else
					goto abort_elem_creation;
				if (chn) {
					ch_name = iio_channel_get_name(chn) ?
							iio_channel_get_name(chn) : iio_channel_get_id(chn);
					if (!ch_name)
						goto abort_elem_creation;
					if (iio_channel_is_output(chn))
						ch_tmp = g_strconcat("out_", ch_name, NULL);
					else
						ch_tmp = g_strconcat("in_", ch_name, NULL);
				} else {
					ch_tmp = g_strdup("global");
				}
				pelement->name = g_strjoin(".", dev_name, ch_tmp, attribute, NULL);
				g_free(ch_tmp);
			break;
		case PROFILE_PLUGIN_ELEMENT:
			pelement->name = g_strdup(attribute);
			break;
		case PROFILE_DEBUG_ELEMENT:
			if (!dev)
				goto abort_elem_creation;
			pelement->name = g_strjoin(".", "debug", iio_device_get_name(dev), attribute, NULL);
			break;
		default:
			break;
	}

	*list = g_slist_prepend(*list, (gpointer)pelement);

	return 0;

abort_elem_creation:
	free(pelement);
	return -1;
}

/* Public Functions */
void profile_elements_init(GSList** list)
{
	*list = NULL;
}

void profile_elements_free(GSList **list)
{
	ProfileElement *p;
	GSList *node;

	for (node = *list; node; node = g_slist_next(node)) {
		p = node->data;
		g_free(p->name);
	}
	g_slist_free(*list);
	*list = NULL;
}

int profile_elements_add_dev_attr(GSList **list, struct iio_device *dev,
		struct iio_channel *chn, const char *attribute)
{
	return profile_elements_add(list, PROFILE_DEVICE_ELEMENT, dev,
			chn, attribute);
}

int profile_elements_add_debug_attr(GSList **list, struct iio_device *dev,
		const char *attribute)
{
	return profile_elements_add(list, PROFILE_DEBUG_ELEMENT, dev,
			NULL, attribute);
}

int profile_elements_add_plugin_attr(GSList **list, const char *attribute)
{
	return profile_elements_add(list, PROFILE_PLUGIN_ELEMENT, NULL,
			NULL, attribute);
}
