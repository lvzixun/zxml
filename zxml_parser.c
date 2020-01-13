#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>
#include <stdarg.h>
#include <assert.h>
#include "zxml.h"

static char cmask[] = {
    'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'S', 'S', 'U', 'U', 'S', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U',
    'S', '!', '"', '#', '$', '%', '&', '\'', 'U', 'U', '*', '+', ',', '-', '.', '/', 'N', 'N', 'N', 'N', 'N', 'N', 'N', 'N', 'N', 'N', ':', ';', '<', '=', '>', '?',
    '@', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', '[', '\\', ']', 'U', 'A',
    'U', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', '{', '|', '}', '~', 'U',
    'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U',
    'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U',
    'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U',
    'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U', 'U',
};

static const char* xml_error_str[] = {
    "XML_SUCCESS",
    "XML_PARSER_ERROR",
    "XML_MALLOC_ERROR",
};


struct xml_context {
    uint8_t* memory_head;
    size_t memory_size;
    uint8_t* memory_tail;

    struct {
        const char* head;
        size_t offset;
        size_t size;
    } reader;

    char error_info[512];
    jmp_buf buf;
    int parser_status;

    struct xml_node* root;
};

#define reader_isend() (context->reader.offset >= context->reader.size)
#define reader_curchar() (assert(!reader_isend()), (context->reader.head[context->reader.offset]))
#define reader_nextchar() (assert(!reader_isend()), (context->reader.head[context->reader.offset++]))
#define reader_curtype() (cmask[(unsigned char)reader_curchar()])
#define reader_curhead() (context->reader.head + context->reader.offset)
#define reader_set(v) (assert((v) > 0 && (v) <= context->reader.size), (context->reader.offset = (v)))
#define reader_offset()  (context->reader.offset)
#define reader_look(o) (context->reader.offset+(o) >= context->reader.size)?('\0'):(context->reader.head[context->reader.offset+(o)])

#define _xml_parser_error(context, f, ...) _xml_error((context), XML_PARSER_ERROR, f, __VA_ARGS__)
#define _xml_errcode2str(e) (assert((e)>0 && (e)<sizeof(xml_error_str)/sizeof(xml_error_str[0])), xml_error_str[(e)])


#define _xml_parser_tagname(context, out_str) _xml_parser_value(context, out_str, "S>/")
#define _xml_parser_tag_fieldname(context, out_str) _xml_parser_value(context, out_str, "S=")
#define _xml_parser_tag_fieldvalue(context, out_str) _xml_parser_value(context, out_str, "S>/")
#define _xml_parser_nodevalue(context, out_str) _xml_parser_value(context, out_str, "<")


#define _xml_expect(context, out_str, f, t) do { \
    bool b = f(context, out_str); \
    if(!b) { \
        _xml_parser_error(context, "expect %s token at offset: %ld", t, reader_offset()); \
    } \
}while(0)
#define _xml_expect_tagname(context, out_str)  _xml_expect(context, out_str, _xml_parser_tagname, "tagname")
#define _xml_expect_fieldname(context, out_str)  _xml_expect(context, out_str, _xml_parser_tag_fieldname, "fieldname")
#define _xml_expect_fieldvalue(context, out_str)  _xml_expect(context, out_str, _xml_parser_tag_fieldvalue, "fieldvalue")
#define _xml_expect_nodevalue(context, out_str)  _xml_expect(context, out_str, _xml_parser_nodevalue, "nodevalue")

#define node_get_element(node) (assert((node)->nt == node_element), &((node)->value.element_value))
#define node_get_content(node) (assert((node)->nt == node_content), &((node)->value.content_value))

static struct xml_node* _xml_parser_entry(struct xml_context* context);


struct xml_context*
xml_create(size_t memory_size) {
    struct xml_context* context = (struct xml_context*)malloc(sizeof(struct xml_context) + memory_size);
    context->memory_head = (uint8_t*)(context + 1);
    context->memory_size = memory_size;
    context->memory_tail = context->memory_head;
    context->root = NULL;
    context->error_info[0] = '\0';
    return context;
}


void
xml_reset(struct xml_context* context) {
    context->memory_tail = context->memory_head;
    context->reader.offset = 0;
    context->error_info[0] = '\0';
    context->parser_status = XML_SUCCESS;
    context->root = NULL;
}


struct xml_node*
xml_parser(struct xml_context* context, const char* xml_source, size_t sz) {
    xml_reset(context);
    context->reader.head = xml_source;
    context->reader.size = sz;

    if(setjmp(context->buf) == 0) {
        // do parser
        struct xml_node* root = _xml_parser_entry(context);
        context->root = root;
        return root;
    } else {
        char* error_p = context->error_info;
        int error_sz = sizeof(context->error_info);
        char tmp[error_sz];
        strncpy(tmp, error_p, sizeof(tmp)-1);
        tmp[error_sz-1]='\0';
        int len = snprintf(error_p, error_sz, "error:[%s] %s\ncurrent_reader:",
            _xml_errcode2str(context->parser_status),
            tmp);
        error_sz -= len;
        error_p  += len;
        if(error_sz > 5) {
            size_t cap = context->reader.size - reader_offset();
            if(cap < error_sz-1) {
                memcpy(error_p, reader_curhead(), cap);
                error_p[cap] = '\0';
            } else {
                size_t sz = error_sz - 5;
                memcpy(error_p, reader_curhead(), sz);
                error_sz -= sz;
                error_p  += sz;
                assert(error_sz >=5);
                snprintf(error_p, error_sz, "...\n");
            }
        }
        return NULL;
    }
}


void
xml_destory(struct xml_context* context) {
    free(context);
}


const char*
xml_geterror(struct xml_context* context) {
    if(context->parser_status != XML_SUCCESS) {
        return (const char*)context->error_info;
    } else {
        return NULL;
    }
}


static bool
_xml_cmpstr(struct xml_str* s1, struct xml_str* s2) {
    return (s1->size == s2->size) && (memcmp(s1->str, s2->str, s1->size)==0);
}


static void
_xml_error(struct xml_context* context, int status, const char* f, ...) {
    va_list args;
    va_start(args, f);
    char* error_info = context->error_info;
    vsnprintf(error_info, sizeof(context->error_info), f, args);
    va_end(args);
    context->parser_status = status;
    longjmp(context->buf, 1);
}


static void*
_xml_malloc(struct xml_context* context, size_t sz) {
    size_t cap = context->memory_size - (context->memory_tail - context->memory_head);
    if(cap < sz) {
        _xml_error(context, XML_MALLOC_ERROR, "xml malloc error");
        return NULL;
    }
    void* p = (void*)context->memory_tail;
    context->memory_tail += sz;
    return p;
}


static struct xml_node*
xml_get_node(struct xml_context* context, enum e_xml_node_type nt) {
    struct xml_node* p = (struct xml_node*)_xml_malloc(context, sizeof(struct xml_node));
    memset(p, 0, sizeof(*p));
    p->nt = nt;
    return p;
}


static struct xml_property*
xml_get_property(struct xml_context* context) {
    struct xml_property* p = (struct xml_property*)_xml_malloc(context, sizeof(struct xml_property));
    p->next = NULL;
    return p;    
}


// match all space
static bool
_xml_parser_blank(struct xml_context* context) {
    bool b = false;
    while(!reader_isend()) {
        char ct = reader_curtype();
        if(ct == 'S') {
            b = true;
        }else {
            break;
        }
        reader_nextchar();
    }
    return b;
}


static bool
_xml_parser_single(struct xml_context* context,  char c) {
    if(!reader_isend() && reader_curchar() == c) {
        reader_nextchar();
        return true;
    }else {
        return false;
    }
}


static bool
_xml_parser_value(struct xml_context* context, struct xml_str* out_str, const char* final_ct_set) {
    bool b = false;
    while(!reader_isend()) {
        char ct = reader_curtype();
        bool in_set = false;
        const char* p = final_ct_set;
        while(*p) {
            if(*p == ct) {
                in_set = true;
                break;
            }
            p++;
        }
        if(!in_set) {
            if(b == false) {
                b = true;
                out_str->str = reader_curhead();
                out_str->size = 0;
            }
            out_str->size++;
        }else {
            break;
        }
        reader_nextchar();
    }
    return b;
}


static bool
_xml_parser_string(struct xml_context* context, struct xml_str* out_str) {
    bool b = false;
    size_t bak_offset = reader_offset();
    char str_begin = reader_look(0);
    if(str_begin != '"' && str_begin != '\'') {
        return false;
    }
    reader_nextchar();

    out_str->str = reader_curhead();
    out_str->size = 0;
    while(!reader_isend()) {
        char c = reader_nextchar();
        if(c == str_begin) {
            b = true;
            break;
        }else {
            out_str->size++;
        }
    }

    if(!b) {
        reader_set(bak_offset);
    }
    return b;
}


inline static void
_xml_expect_single(struct xml_context* context, char c) {
    bool b = _xml_parser_single(context, c);
    if(!b) {
        char lcs[2] = {0};
        char* plc = lcs;
        lcs[0] = reader_look(0);
        if(lcs[0] == 0) {
            plc = "EOF";
        }
        _xml_parser_error(context, "expecet char `%c` current `%s` at offset: %ld", c, plc, reader_offset());
    }
}

inline static void
_xml_expect_string(struct xml_context* context, struct xml_str* out_str) {
    bool b = _xml_parser_string(context, out_str);
    if(!b) {
        _xml_parser_error(context, "expect string at offset: %ld", reader_offset());
    }
}


static void
_xml_parser_property(struct xml_context* context, struct xml_property* property) {
    _xml_expect_fieldname(context, &property->field_name);
    _xml_expect_single(context, '=');
    if(!reader_isend()) {
        char c = reader_curchar();
        if(c == '"' || c == '\'') {
            _xml_expect_string(context, &property->field_value);
        }else {
            _xml_expect_fieldvalue(context, &property->field_value);
        }
    }else {
        _xml_parser_error(context, "not expect EOF when parser property at offset: %ld", reader_offset());
    }
}


// match <?xml version="1.0"?>
static void
_xml_parser_tagheader(struct xml_context* context) {
    _xml_expect_single(context, '<');
    _xml_expect_single(context, '?');
    struct xml_str str;
    struct xml_property p;
    _xml_parser_tagname(context, &str);
    _xml_parser_blank(context);
    while(!reader_isend()) {
        char ct = reader_curtype();
        if(ct == '?') {
            break;
        }else {
            _xml_parser_property(context, &p);
            _xml_parser_blank(context);
        }
    }
    _xml_expect_single(context, '?');
    _xml_expect_single(context, '>');
}


static struct xml_node*
_xml_parser_tag(struct xml_context* context) {
    //  match begin tag
    _xml_expect_single(context, '<');
    struct xml_node* node = xml_get_node(context, node_element);
    struct xml_element* element = node_get_element(node);
    _xml_expect_tagname(context, &element->tag);
    _xml_parser_blank(context);
    while(!reader_isend()) {
        char ct = reader_curtype();
        if(ct == '/') {
            _xml_expect_single(context, '/');
            _xml_expect_single(context, '>');
            return node;
        }else if (ct == '>') {
            _xml_expect_single(context, '>');
            break;
        } else {
            struct xml_property* p = xml_get_property(context);
            _xml_parser_property(context, p);
            p->next = element->attrs;
            element->attrs = p;
            _xml_parser_blank(context);
        }
    }

    // match children
    _xml_parser_blank(context);
    while(!reader_isend()) {
        struct xml_node* cnode = NULL;
        char c = reader_look(0);
        if(c == '<') {
            char c0 = reader_curchar();
            char c1 = reader_look(1);
            if(c0 == '<' && c1 == '/') {
                break;
            }
            cnode = _xml_parser_tag(context);
            _xml_parser_blank(context);
        } else {
            cnode = xml_get_node(context, node_content);
            _xml_expect_nodevalue(context, node_get_content(cnode));
        }
        assert(cnode);
        if(element->children_tail) {
            element->children_tail->next = cnode;
            cnode->next = NULL;
        }
        element->children_tail = cnode;
        if(!element->children_head) {
            element->children_head = cnode;
        }
    }

    // match end tag
    _xml_expect_single(context, '<');
    _xml_expect_single(context, '/');
    struct xml_str endtag_name;
    _xml_expect_tagname(context, &endtag_name);
    _xml_expect_single(context, '>');

    if(!_xml_cmpstr(&element->tag, &endtag_name)) {
        char s1[element->tag.size + 1];
        char s2[endtag_name.size + 1];
        memcpy(s1, element->tag.str, element->tag.size);
        memcpy(s2, endtag_name.str, endtag_name.size);
        s1[element->tag.size] ='\0';
        s2[endtag_name.size] = '\0';
        _xml_parser_error(context, "inconsistent tag name `%s` and `%s` at offset:%ld", s1, s2, reader_offset());
    }
    return node;
}


static struct xml_node*
_xml_parser_entry(struct xml_context* context) {
    _xml_parser_blank(context);
    while(!reader_isend()) {
        char c0 = reader_look(0);
        char c1 = reader_look(1);
        if(c0 == '<' && c1 == '?') {
            _xml_parser_tagheader(context);
        } else {
            break;
        }
        _xml_parser_blank(context);
    }
    
    _xml_parser_blank(context);
    struct xml_node* root = _xml_parser_tag(context);
    return root;
}
