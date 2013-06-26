/**
 * Copyright 2012-2013(c) Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/

#ifndef __XML_UTILS_H__
#define __XML_UTILS_H__

void set_xml_folder_path(char *path);
int open_xml_file(char *file_name, xmlNodePtr *root);
char* read_string_element(xmlNodePtr node, char *element);
int read_integer_element(xmlNodePtr node, char *element);
xmlXPathObjectPtr retrieve_all_elements(char *element);
xmlNodePtr get_child_by_name(xmlNodePtr parent_node, char* tag_name);
xmlNodePtr* get_children_by_name(xmlNodePtr parent_node, char* tag_name, 
							int *children_cnt);
void close_current_xml_file(void);

#endif
