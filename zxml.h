#ifndef __ZXML_HEAD__
#define __ZXML_HEAD__

#include <stddef.h>


#define XML_SUCCESS      0
#define XML_PARSER_ERROR 1
#define XML_MALLOC_ERROR 2

enum e_xml_node_type {
    node_element,
    node_content,
};

struct xml_str {
    const char* str;
    int size;
};

struct xml_property {
    struct xml_str field_name;
    struct xml_str field_value;
    struct xml_property* next;
};

struct xml_element {
    struct xml_str tag;
    struct xml_property* attrs;
    struct xml_node* children_head;
    struct xml_node* children_tail;
};

struct xml_node {
    enum e_xml_node_type nt;
    union {
        struct xml_element element_value;
        struct xml_str     content_value;
    } value;
    struct xml_node* next;
};


struct xml_context;
struct xml_context* xml_create(size_t memory_size);
void xml_reset(struct xml_context* context);
void xml_destory(struct xml_context* context);

struct xml_node* xml_parser(struct xml_context* context, const char* xml_source, size_t sz);
const char* xml_geterror(struct xml_context* context);

#endif

