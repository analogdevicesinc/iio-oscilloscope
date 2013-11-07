/**
 * Copyright 2012-2013(c) Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/

#ifndef __XML_UTILS_H__
#define __XML_UTILS_H__

xmlDocPtr open_xml_file(char *file_name, xmlNodePtr *root);
void find_device_xml_file(char *dir_path, char *device_name, char *xml_name);
char* read_string_element(xmlDocPtr doc, xmlNodePtr node, char *element);
int read_integer_element(xmlDocPtr doc, xmlNodePtr node, char *element);
xmlXPathObjectPtr retrieve_all_elements(xmlDocPtr doc, char *element);
xmlNodePtr get_child_by_name(xmlNodePtr parent_node, char* tag_name);
xmlNodePtr* get_children_by_name(xmlNodePtr parent_node, char* tag_name,
							int *children_cnt);
void close_xml_file(xmlDocPtr doc);

#endif
