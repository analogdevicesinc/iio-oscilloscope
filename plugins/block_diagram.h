/**
 * Copyright (C) 2012-2014 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/

#ifndef __OSC_PLUGIN_BLOCK_H__
#define __OSC_PLUGIN_BLOCK_H__

#include <stdarg.h>
#include <stdint.h>

static double scale_block = 1.0;
static int x_block = 0, y_block = 0, redraw_block = 0;
static GtkWidget *block_diagram_events;
static GtkWidget *next_pict, *previous_pict;
static char *block_filename[256];
static unsigned long block_num = 0;

static gboolean zoom_image_press_cb (GtkWidget *event_box, GdkEventButton *event, gpointer data)
{
	x_block = event->x;
	y_block = event->y;
	redraw_block = 1;

	gtk_widget_queue_draw(block_diagram_events);

	return TRUE;
}

static void next_image_cb (GtkButton *btn, gpointer data)
{
	block_num += (uintptr_t) data;

	gtk_widget_set_sensitive(next_pict, block_filename[block_num + 1] != NULL);
	gtk_widget_set_sensitive(previous_pict, block_num != 0);

	scale_block = 1;
	redraw_block = 1;
	gtk_widget_queue_draw(block_diagram_events);
}

static void zoom_image_cb (GtkButton *btn, gpointer data)
{
	switch ((uintptr_t) data) {
		case 0:
			scale_block += .1;
			break;
		case 1:
			if (scale_block != 1.0)
				scale_block -= .1;
			break;
		case 2:
			scale_block = 1.0;
			break;
	}

	gtk_widget_queue_draw(block_diagram_events);

}

static gboolean erase_block_diagram(GtkNotebook *notebook, GtkWidget *page, guint page_num, GtkImage *block_diagram)
{

	if (page_num) {
		redraw_block = 1;
		return true;
	}

	gtk_image_set_from_pixbuf(block_diagram, NULL);
	return true;
}

static gboolean draw_block_diagram(GtkWidget *widget, cairo_t *cr, GtkImage *block_diagram)
{
	GdkPixbuf *pixbuf, *sub_pixbuf;
	static GdkPixbuf *p = NULL;
	GError *err = NULL;
	static int x = 0, y = 0;
	static double scale = 0;
	int x_big, y_big, x_click, y_click, x_in, y_in;
	char name[256];

	if (y == widget->allocation.height && x == widget->allocation.width &&
			scale == scale_block && !redraw_block)
		return false;

	x = widget->allocation.width;
	y = widget->allocation.height;
	x_in = x * 0.95;
	y_in = y * 0.95;
	x_big = x * scale_block;
	y_big = y * scale_block;
	scale = scale_block;
	redraw_block = 0;

	sprintf(name, "./block_diagrams/%s", block_filename[block_num]);
	if (access(name, F_OK) != 0)
		sprintf(name, "%s/block_diagrams/%s", OSC_PLUGIN_PATH, block_filename[block_num]);

	if (strstr(block_filename[block_num], ".svg"))
		pixbuf = gdk_pixbuf_new_from_file_at_scale(name, x_big , y_big, false, &err);
	else {
		pixbuf = gdk_pixbuf_new_from_file_at_scale(name, x_in , y_in, true, &err);
		x_in = gdk_pixbuf_get_width(pixbuf);
		y_in = gdk_pixbuf_get_height(pixbuf);
		scale_block = 1.0;
	}

	if (err || pixbuf == NULL) {
		sprintf(name, "%s/block_diagrams/%s", OSC_PLUGIN_PATH, block_filename[block_num]);
		err = NULL;
		pixbuf = gdk_pixbuf_new_from_file_at_scale(name, x_big , y_big, false, &err);
		if (err || pixbuf == NULL) {
			printf("failed to get image %s\n", name);
			return false;
		}
	}

	if (p)
		g_object_unref(p);

	if (scale_block != 1.0) {
		x_click = x_block * (x_big - x_in) / x_in;
		y_click = y_block * (y_big - y_in) / y_in;

		if (x_click < 0)
			x_click = 0;
		if (x_click + x_in > x_big)
			x_click = x_big - x_in;
		if (y_click < 0)
			y_click = 0;
		if (y_click + y_in > y_big)
			y_click = y_big - y_in;

		sub_pixbuf = gdk_pixbuf_new_subpixbuf(pixbuf, x_click, y_click, x_in, y_in);
		p = gdk_pixbuf_scale_simple(sub_pixbuf, x_in, y_in, GDK_INTERP_BILINEAR);
		g_object_unref(sub_pixbuf);
	} else {
		p = gdk_pixbuf_scale_simple(pixbuf, x_in, y_in, GDK_INTERP_BILINEAR);
	}

	g_object_unref(pixbuf);

	gtk_image_set_from_pixbuf(block_diagram, p);

	return false;
}

static int block_diagram_init(GtkBuilder *builder, int count, ...)
{
	GtkImage  *block_diagram;
	GtkNotebook *noteb;
	va_list ap;
	int argno = 0;

	va_start(ap, count);
	while (count--) {
		block_filename[argno++] = va_arg(ap, char *);
	}
#ifdef linux
	FILE *cpu_fd = fopen("/proc/cpuinfo", "rb");
	char *line = NULL;
	size_t n;
	bool is_zynq = false;

	if (cpu_fd) {
		while (getline(&line, &n, cpu_fd) != -1) {
			if (!strncmp(line, "Hardware", strlen("Hardware"))
							&& strstr(line, "Zynq"))
				is_zynq = true;
			free(line);
			line = NULL;
		}
		fclose(cpu_fd);
	}
	if (is_zynq) {
		block_filename[argno++] = "Zynq.svg";
	}
#endif
	block_filename[argno] = NULL;
	block_num = 0;

	va_end(ap);

	block_diagram = GTK_IMAGE(gtk_builder_get_object(builder, "block_diagram"));
	block_diagram_events = GTK_WIDGET(gtk_builder_get_object(builder, "block_diagram_events"));
	next_pict = GTK_WIDGET(gtk_builder_get_object(builder, "next_pict"));
	previous_pict = GTK_WIDGET(gtk_builder_get_object(builder, "previous_pict"));

	g_signal_connect(block_diagram_events, "expose-event", G_CALLBACK(draw_block_diagram),
			block_diagram);

	noteb = GTK_NOTEBOOK(gtk_builder_get_object(builder, "plugin_notebook"));
	g_signal_connect(noteb, "switch-page", G_CALLBACK(erase_block_diagram),
			block_diagram);

	g_builder_connect_signal(builder, "zoom_image", "clicked", G_CALLBACK(zoom_image_cb), (gpointer *)0);
	g_builder_connect_signal(builder, "unzoom_image", "clicked", G_CALLBACK(zoom_image_cb), (gpointer *)1);
	g_builder_connect_signal(builder, "auto_image", "clicked", G_CALLBACK(zoom_image_cb), (gpointer *)2);
	g_builder_connect_signal(builder, "block_diagram_events", "button_press_event", G_CALLBACK(zoom_image_press_cb), NULL);
	g_builder_connect_signal(builder, "next_pict", "clicked", G_CALLBACK(next_image_cb), (gpointer *)1);
	g_builder_connect_signal(builder, "previous_pict", "clicked", G_CALLBACK(next_image_cb), (gpointer *)-1);

	if (count == 1) {
		gtk_widget_hide(next_pict);
		gtk_widget_hide(previous_pict);
	}

	next_image_cb(NULL, NULL);

	return 0;
}

#endif
