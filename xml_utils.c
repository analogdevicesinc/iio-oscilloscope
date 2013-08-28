/**
 * Copyright 2012-2013(c) Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/

#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>

#include "xml_utils.h"

#define MAX_STR_LEN			512
static char      buf_dir_name[MAX_STR_LEN];
static xmlDocPtr doc_ADI_device;

/*
 * Set the path of the folder where xml files can be found. The path can be
 * relative to the path of this file or the full path.
 */
void set_xml_folder_path(char *path)
{
	sprintf(buf_dir_name, "%s", path);
}

/*
 * Open a xml file with the given name and store in the root variable a pointer
 * to the first element of the xml file. Add extension to the filename if it
 * does not have one.
 * Return 0 - when the file opened successfully or -1 otherwise.
 */
int open_xml_file(char *file_name, xmlNodePtr *root)
{
	char *has_ext;
	char *extension = ".xml";
	char *temp;

	if ((root == NULL) || (file_name == NULL))
		return -1;

	if (strlen(file_name) == 0)
		return -1;

	temp  = (char *)malloc(strlen(buf_dir_name) + strlen(file_name) +
							strlen(extension) +
							1 + /* for the "/" character between buf_dir_name and file_name */
							1); /* for the null terminator */
	if (temp == NULL) {
		printf("Memory allocation failed");
		return -1;
	}

	/* Add extension to a path without one. */
	has_ext = strstr(file_name, extension);
	if (has_ext == NULL)
		sprintf(temp, "%s/%s%s", buf_dir_name, file_name, extension);
	else
		sprintf(temp, "%s/%s", buf_dir_name, file_name);
	doc_ADI_device = xmlParseFile(temp);
	if (doc_ADI_device == NULL){
		free(temp);
		return -1;
	}
	*root = xmlDocGetRootElement(doc_ADI_device);
	if (*root == NULL){
		printf("%s is empty (%d)\n", temp, __LINE__);
		xmlFreeDoc(doc_ADI_device);
		return -1;
	}
	free(temp);

	return 0;
}

/*
 * Create a list of char arrays that hold the names of the xml files stored in
 * the directory provided by the caller.
 * Pass to the caller the number of elements in the list, using list_size.
 * Return a pointer to the list.
 */
char **get_xml_list(int *list_size)
{
	const struct dirent *ent;
	DIR *d;
	char **list;
	char *extension_ptr;
	int cnt = 0;
	int n = 0;

	d = opendir(buf_dir_name);
	if (!d) {
		printf("Cannot open dir %s\n", buf_dir_name);
		return NULL;
	}

	list = (char **)malloc(sizeof(char *) * 0);
	if (list == NULL) {
		printf("Memory allocation failed\n");
		return NULL;
	}
	while (ent = readdir(d), ent != NULL) {
		if (ent->d_type == DT_REG) { /* if the entry is a regular file */
			extension_ptr = strstr(ent->d_name, ".xml");
			if (extension_ptr != NULL) { /* if the entry has a ".xml" extension */
				cnt++;
				list = (char **)realloc(list, sizeof(char *) * cnt);
				n = extension_ptr - ent->d_name;
				n += 1; /* 1 is for the termination character */
				list[cnt - 1] = (char *)malloc(sizeof(char) * n);
				if (list[cnt - 1] == NULL) {
					printf("Memory allocation failed\n");
					return NULL;
				}
				snprintf(list[cnt - 1], n,  "%s", ent->d_name);
			}
		}
	}
	closedir(d);
	*list_size = cnt;

	return list;
}

/*
 * Free the memory allocated by get_xml_list().
 */
void free_xml_list(char **list, int list_size)
{
	int i;

	for (i = 0; i < list_size; i++) {
		free(list[i]);
	}
	free(list);
}

/*
 * Search for the xml file designed for the device provided by the caller.
 * This is done by checking if the name of the device contains the name
 * of the xml file.
 * Return none, but use xml_name to return the name of the xml file that
 * is found.
 */
void find_device_xml_file(char *device_name, char *xml_name)
{
	char **xmls_list;
	char *xml_elem;
	int size = 0;
	int i;

	xmls_list = get_xml_list(&size);
	for(i = 0; i < size; i++) {
		xml_elem = strstr(device_name, xmls_list[i]);
		if (xml_elem != NULL) { /* if the element name from the xml list exist within the device name */
			snprintf(xml_name, MAX_STR_LEN, "%s", xmls_list[i]);
			break;
		}
	}
	free_xml_list(xmls_list, size);
	/* Empty the string if no names were found */
	if (i >= size) {
		snprintf(xml_name, MAX_STR_LEN, "%s", "");
	}
}

/*
 * Search the content of specified element that belongs to the node. The search
 * stops at the first found element.
 * Return - the element value as a char array. Returns an empty string ("") if
 * no element is found.
 */
char* read_string_element(xmlNodePtr node, char *element)
{
	char *text = NULL;

	node = node->xmlChildrenNode;
	while (node != NULL){
		if (!xmlStrcmp(node->name, (xmlChar *)element)){
			text = (char *)xmlNodeListGetString(doc_ADI_device, node->xmlChildrenNode, 1);
			break;
		}
		node = node->next;
	}
	if (text == NULL){
		text = (char *)calloc(1, sizeof(char)); /* Create an empty string */
	}

	return text;
}

/*
 * Search the content of specified element that belongs to the node. The search
 * stops at the first found element.
 * Return - the element value as an integer. Returns 0 if no element is found.
 */
int read_integer_element(xmlNodePtr node, char *element)
{
	char *text = NULL;
	int result;
	int ret;

	text = read_string_element(node, element);
	if (*text == 0) /* Check if the string is empty */
		return 0;
	ret = sscanf(text, "%d", &result);
	xmlFree(text);
	if (ret != 1){
		printf("Could not convert xml string to integer \n");
		return 0;
	}

	return result;
}

/*
 * Use XPath for the search. Find all elements with the name specified by user.
 * Return - pointer to the list of elements or NULL if an error occurred.
 */
xmlXPathObjectPtr retrieve_all_elements(char *element)
{
	xmlXPathContextPtr context;
	xmlXPathObjectPtr result;

	context = xmlXPathNewContext(doc_ADI_device);
	if (context == NULL){
		printf("Error in xmlXPathNewContext\n");
		return NULL;
	}
		result = xmlXPathEvalExpression((xmlChar *)element, context);
		xmlXPathFreeContext(context);
	if (result == NULL){
		printf("Error in xmlXPathEvalExpression\n");
		return NULL;
	}
	if (xmlXPathNodeSetIsEmpty(result->nodesetval)){
		xmlXPathFreeObject(result);
		printf("No result.\n");
		return NULL;
	}

	return result;
}

/*
 * Search for the child with the specified name. The search stops at the first
 * found child.
 * Return - the child reference or NULL when none are found.
 */
xmlNodePtr get_child_by_name(xmlNodePtr parent_node, char* tag_name)
{
	xmlNodePtr child_node;

	child_node = parent_node->xmlChildrenNode;

	/* Search through the list of the available children */
	while (child_node != NULL){
		if (!xmlStrcmp(child_node->name, (xmlChar *)tag_name)){
			return child_node;
		}
		child_node = child_node->next;
	}

	return NULL;
}

/*
 * Search for all children with the specified name and create a children list.
 * The size of the list is stored in the children_cnt parameter.
 * Return - a pointer to the list, the caller must free it with free().
 */
xmlNodePtr* get_children_by_name(xmlNodePtr parent_node, char* tag_name, int *children_cnt)
{
	xmlNodePtr *children_list;
	xmlNodePtr child_node;
	int n;

	child_node = parent_node->xmlChildrenNode;
	n = 0;
	children_list = (xmlNodePtr *)malloc(sizeof(xmlNodePtr) * n);

	/* Search through the list of the available children */
	while (child_node != NULL){
		if (!xmlStrcmp(child_node->name, (xmlChar *)tag_name)){
			n++;
			children_list = (xmlNodePtr *)realloc(children_list, sizeof(xmlNodePtr) * n);
			children_list[n - 1] = child_node;
		}
		child_node = child_node->next;
	}
	*children_cnt = n;

	return children_list;
}

void close_current_xml_file(void)
{
	xmlFreeDoc(doc_ADI_device);
}
