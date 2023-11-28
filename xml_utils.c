/**
 * Copyright 2012-2013(c) Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>

#include "compat.h"
#include "xml_utils.h"

#define MAX_STR_LEN    512

/*
 * Open a xml file with the given name and store in the root variable a pointer
 * to the first element of the xml file. Add extension to the filename if it
 * does not have one.
 * Return a reference to the opened xml file or NULL if an error occurs.
 */
xmlDocPtr open_xml_file(char *file_name, xmlNodePtr *root)
{
	char *has_ext;
	char *extension = ".xml";
	char *temp;
	xmlDocPtr doc;

	if ((root == NULL) || (file_name == NULL))
		return NULL;

	if (strlen(file_name) == 0)
		return NULL;

	temp  = (char *)malloc(strlen(file_name) +
							strlen(extension) +
							1); /* for the null terminator */
	if (temp == NULL) {
		printf("Memory allocation failed");
		return NULL;
	}

	/* Add extension to a path without one. */
	has_ext = strstr(file_name, extension);
	if (has_ext == NULL)
		sprintf(temp, "%s%s", file_name, extension);
	else
		sprintf(temp, "%s", file_name);
	doc = xmlParseFile(temp);
	if (doc == NULL){
		free(temp);
		return NULL;
	}
	*root = xmlDocGetRootElement(doc);
	if (*root == NULL){
		printf("%s is empty (%d)\n", temp, __LINE__);
		xmlFreeDoc(doc);
		free(temp);
		return NULL;
	}
	free(temp);

	return doc;
}

/*
 * Create a list of char arrays that hold the names of the xml files stored in
 * the directory provided by the caller.
 * Pass to the caller the number of elements in the list, using list_size.
 * Return a pointer to the list.
 */
char **get_xml_list(char * buf_dir_name, int *list_size)
{
	const struct dirent *ent;
	DIR *d;
	char **list = NULL, **list1 = NULL;
	char *extension_ptr;
	int cnt = 0;
	int n = 0;

	d = opendir(buf_dir_name);
	if (!d) {
		printf("Cannot open dir %s\n", buf_dir_name);
		return NULL;
	}

	while ((ent = readdir(d))) {
		if (is_dirent_reqular_file(ent)) {
			extension_ptr = strstr(ent->d_name, ".xml");
			if (extension_ptr != NULL) { /* if the entry has a ".xml" extension */
				cnt++;
				list1 = (char **)realloc(list, sizeof(char *) * cnt);
				if (!list1) {
					printf("Memory allocation failed\n");
					free(list);
					closedir(d);
					return NULL;
				}
				list = list1;
				n = extension_ptr - ent->d_name;
				n += 1; /* 1 is for the termination character */
				list[cnt - 1] = (char *)malloc(sizeof(char) * n);
				if (list[cnt - 1] == NULL) {
					printf("Memory allocation failed\n");
					for (; cnt >= 2; cnt--)
						free(list[cnt - 2]);
					free(list);
					closedir(d);
					return NULL;
				}
				snprintf(list[cnt - 1], n,  "%s", ent->d_name);
				list[cnt - 1][n - 1] = '\0'; /* Required on MinGW */
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
void find_device_xml_file(char *dir_path, char *device_name, char *xml_name)
{
	char **xmls_list;
	char *xml_elem;
	int size = 0;
	int i;

	xmls_list = get_xml_list(dir_path, &size);
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
char* read_string_element(xmlDocPtr doc, xmlNodePtr node, char *element)
{
	char *text = NULL;

	node = node->xmlChildrenNode;
	while (node != NULL){
		if (!xmlStrcmp(node->name, (xmlChar *)element)){
			text = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
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
int read_integer_element(xmlDocPtr doc, xmlNodePtr node, char *element)
{
	char *text;
	int result;
	int ret;

	text = read_string_element(doc, node, element);
	if (*text == 0) { /* Check if the string is empty */
		xmlFree(text);
		return 0;
	}
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
xmlXPathObjectPtr retrieve_all_elements(xmlDocPtr doc, char *element)
{
	xmlXPathContextPtr context;
	xmlXPathObjectPtr result;

	context = xmlXPathNewContext(doc);
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
	xmlNodePtr *children_list = NULL, *new_lst;
	xmlNodePtr child_node;
	int n = 0;

	child_node = parent_node->xmlChildrenNode;

	/* Search through the list of the available children */
	while (child_node != NULL){
		if (!xmlStrcmp(child_node->name, (xmlChar *)tag_name)){
			n++;
			new_lst = (xmlNodePtr *)realloc(children_list, sizeof(xmlNodePtr) * n);
			if (!new_lst) {
				printf("Memory allocation failed\n");
				free(children_list);
				return NULL;
			}
			children_list = new_lst;
			children_list[n - 1] = child_node;
		}
		child_node = child_node->next;
	}
	*children_cnt = n;

	return children_list;
}

void close_xml_file(xmlDocPtr doc)
{
	xmlFreeDoc(doc);
}
