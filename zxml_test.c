#include "zxml.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

static void
_xml_printtab(int tab) {
    int i;
    for(i=0; i<tab; i++) {
        printf("  ");
    }
}

static void
_xml_printstr(struct xml_str* str, int tab) {
    if(str->size > 0) {
        _xml_printtab(tab);
        char s[str->size+1];
        memcpy(s, str->str, str->size);
        s[str->size] = '\0';
        printf("%s", s);
    }
}


static void
_xml_dump_node(struct xml_node* node, int tab) {
    _xml_printtab(tab);
    enum e_xml_node_type nt = node->nt;
    if(nt == node_element) {
        struct xml_element* element = &node->value.element_value;
        printf("[element:]\n");
        _xml_printtab(tab);
        printf("[tag:]\n");
        _xml_printstr(&element->tag, tab);
        printf("\n");
        _xml_printtab(tab);
        printf("[attrs:]\n");
        struct xml_property* p = element->attrs;
        while(p) {
            _xml_printtab(tab);
            _xml_printstr(&p->field_name, tab);
            printf(" = ");
            _xml_printstr(&p->field_value, 0);
            printf("\n");
            p = p->next;
        }
        _xml_printtab(tab);
        printf("[children:]\n");
        struct xml_node* c = element->children_head;
        while(c) {
            _xml_dump_node(c, tab+1);
            c = c->next;
        }

    } else if(nt == node_content) {
        printf("[context:]\n");
        _xml_printstr(&node->value.content_value, tab);
        printf("\n");

    } else {
        assert(false);
    }
}


// for test
int 
main(int argc, char const *argv[]) {
    assert(argc == 2);
    const char* xml_path = argv[1];
    printf("xml_path: %s\n", xml_path);
    FILE* fp = fopen(xml_path, "r");
    if(!fp) {
        printf("open xml_path:%s error\n", xml_path);
        return 0;
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char* source = malloc(size+1);
    fread(source, size, 1, fp);
    source[size+1] = '\0';

    struct xml_context* context = xml_create(size*8);
    assert(context);
    struct xml_node* root = xml_parser(context, source, size);
    printf("context:%p root:%p\n", context, root);
    printf("\n--------------\n");
    if (root) {
        _xml_dump_node(root, 0);
    } else {
        printf("xml parser:%s\n", xml_geterror(context));
    }
    return 0;
}