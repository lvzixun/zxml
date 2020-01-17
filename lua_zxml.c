#include "zxml.h"
#include <stdlib.h>
#include <string.h>
#include <lua.h>
#include <lauxlib.h>
#include <stdbool.h>
#include <assert.h>

static struct {
    struct xml_context* context;
    size_t memory_size;
}LAST_ZXML_CONTEXT;

#define get_context()  (LAST_ZXML_CONTEXT.context)

#define do_resolve_children(element, f, idx, params) do{ \
    struct xml_node* c = (element)->children_head; \
    while(c) { \
        f(L, c, (idx), (params)); \
        c = c->next; \
    } \
}while(0)

#define lua_pushxmlstr(L, xmlstr) (lua_pushlstring((L), (xmlstr)->str, (xmlstr)->size))

struct xml_params {
    unsigned int worksheet;
    unsigned int table;
    unsigned int row;
    unsigned int cell;
    unsigned int children;
    unsigned int max_col_of_row1;
    luaL_Buffer* data_buffer;
};


static void
_init_context(lua_State* L, size_t new_memory_size) {
    if(get_context() == NULL || LAST_ZXML_CONTEXT.memory_size < new_memory_size) {
        if(get_context() != NULL) {
            xml_destory(get_context());
        }
        LAST_ZXML_CONTEXT.context = xml_create(new_memory_size);
        if(get_context() == NULL) {
            luaL_error(L, "new context error");
        }
        LAST_ZXML_CONTEXT.memory_size = new_memory_size;
    } else {
        xml_reset(get_context());
    }
}


inline static bool
_check_name(struct xml_str* str, const char* s) {
    size_t l = strlen(s);
    return (l == str->size) && (memcmp(str->str, s, l)==0);
}


static struct xml_str*
_get_propertyvalue(struct xml_property* attrs, const char* s) {
    struct xml_property* p = attrs;
    while(p) {
        if(_check_name(&p->field_name, s)) {
            return &p->field_value;
        }
        p = p->next;
    }
    return NULL;
}


static struct escape_str {
    const char* s;
    char c;
} ESCAPE_LIST[] = {
    {NULL, '\0'},
};

inline static void
_do_content(lua_State* L, struct xml_str* content, luaL_Buffer* data_buffer) {
    int l = content->size;
    const char* s = content->str;
    const char* sub_str = s;
    int sub_sz = 0;
    for(int i=0; i<l; i++){
        char c = *s;
        // escape char
        if(c == '&') {
            sub_sz = s - sub_str;
            luaL_addlstring(data_buffer, sub_str, sub_sz);
            s++;
            i++;
            sub_str = s;
            sub_sz = 0;
            int j;
            for(j=i; j<l; j++) {
                c = *s;
                if(c == ';') {
                    sub_sz = j - i;
                    i = j;
                    break;
                }
                s++;
            }
            if(sub_sz <= 0) {
                luaL_error(L, "escape char error");
            }
            char ss = *sub_str;
            char ret_char = '\0';
            switch(ss) {
                case 'l': {
                    if(sub_sz == 2 && *(sub_str+1) == 't') {
                        ret_char = '<';
                    } else {
                        goto _DO_OTHER_ESCAPE_CHAR;
                    }
                }; break;
                case 'g': {
                    if(sub_sz == 2 && *(sub_str+1) == 't') {
                        ret_char = '>';
                    } else {
                        goto _DO_OTHER_ESCAPE_CHAR;
                    }
                }; break;
                case 'q': {
                    if(sub_sz == 4 && 
                       *(sub_str+1) == 'u' &&
                       *(sub_str+2) == 'o' &&
                       *(sub_str+3) == 't'  ) {
                        ret_char = '"';
                    } else {
                        goto _DO_OTHER_ESCAPE_CHAR;
                    }
                }; break;
                case 'a': {
                    if(sub_sz == 3 && 
                       *(sub_str+1) == 'm' &&
                       *(sub_str+2) == 'p'  ) {
                        ret_char = '&';
                    } else if (sub_sz == 4 &&
                       *(sub_str+1) == 'p' && 
                       *(sub_str+2) == 'o' &&
                       *(sub_str+3) == 's'  ) {
                        ret_char = '\'';
                    } else {
                        goto _DO_OTHER_ESCAPE_CHAR;
                    }
                }; break;
                case '#': {
                    if(sub_sz <= 4) {
                        int v=0;
                        int base=1;
                        int k;
                        for(k=sub_sz-1; k>=1; k--) {
                            char kc = *(sub_str+k);
                            if(kc>='0' && kc<='9') {
                                int cv = (int)(kc - '0');
                                cv *= base;
                                v += cv;
                                base *= 10;
                            }else {
                                luaL_error(L, "invalid &#<number> escape char");
                            }
                        }
                        if(v >0 && v<0x7f) {
                            ret_char = (char)v;
                        } else {
                            ret_char = ' '; // convert invalid ascii code to space
                        }
                    } else {
                        goto _DO_OTHER_ESCAPE_CHAR;
                    }
                }; break;
_DO_OTHER_ESCAPE_CHAR:
                default: {
                    struct escape_str* p = ESCAPE_LIST;
                    struct xml_str str;
                    str.str = sub_str;
                    str.size = sub_sz;
                    while(p->s) {
                        if(_check_name(&str, p->s)) {
                            ret_char = p->c;
                            break;
                        }
                        p++;
                    }
                    if(!p->s) {
                        luaL_checkstack(L, 1, NULL);
                        lua_pushxmlstr(L, &str);
                        const char* error_escape_str = lua_tostring(L, -1);
                        luaL_error(L, "invalid escape string:%s", error_escape_str);
                    }
                }; break;
            }
            assert(ret_char);
            luaL_addchar(data_buffer, ret_char);
            sub_str += sub_sz + 1;
            sub_sz = 0;
        } else {
            sub_sz++;
        }
        s++;
    }
    if(sub_sz>0) {
        luaL_addlstring(data_buffer, sub_str, sub_sz);
    }
}


static void
_resolve_content(lua_State* L, struct xml_node* node, int ret_tbl_idx, struct xml_params* params) {
    enum e_xml_node_type nt = node->nt;
    if(nt == node_element) {
        struct xml_element* element = &node->value.element_value;
        do_resolve_children(element, _resolve_content, ret_tbl_idx, params);
    } else if (nt == node_content) {
        struct xml_str* content = &node->value.content_value;
        // luaL_addlstring(params->data_buffer, content->str, content->size);
        _do_content(L, content, params->data_buffer);
    } else {
        luaL_error(L, "invalid node_type:%d", nt);
    }
}


static void
_resolve_data(lua_State* L, struct xml_node* node, int ret_tbl_idx, struct xml_params* params) {
    enum e_xml_node_type nt = node->nt;
    if(nt == node_element) {
        struct xml_element* element = &node->value.element_value;
        if(_check_name(&element->tag, "Data") || _check_name(&element->tag, "ss:Data")) {
            do_resolve_children(element, _resolve_content, ret_tbl_idx, params);
        }
    }
}


static void
_resolve_cell(lua_State* L, struct xml_node* node, int ret_tbl_idx, struct xml_params* params) {
    unsigned int max_col_of_row1 = params->max_col_of_row1;
    if(max_col_of_row1>0 && params->cell>=max_col_of_row1) {
        return;
    }

    enum e_xml_node_type nt = node->nt;
    if(nt == node_element) {
        struct xml_element* element = &node->value.element_value;
        if(_check_name(&element->tag, "Cell")) {
            params->cell++;
            luaL_checkstack(L, 2, NULL);
            // set empty cell
            struct xml_str* cell_index = _get_propertyvalue(element->attrs, "ss:Index");
            if(cell_index) {
                lua_pushxmlstr(L, cell_index);
                int isnum;
                lua_Integer cidx = lua_tointegerx(L, -1, &isnum);
                if(!isnum || cidx < params->cell) {
                    luaL_error(L, "invalid cell ss:Index");
                }
                lua_pop(L, 1);
                while(cidx > params->cell) {
                    lua_pushstring(L, "");
                    lua_seti(L, ret_tbl_idx, params->cell);
                    params->cell++;
                    if(max_col_of_row1>0 && params->cell>max_col_of_row1) {
                        return;
                    }
                }
            }
            luaL_Buffer B;
            params->data_buffer = &B;
            luaL_buffinit(L, &B);
            do_resolve_children(element, _resolve_data, ret_tbl_idx, params);
            luaL_pushresult(&B);
            lua_seti(L, ret_tbl_idx, params->cell);
            params->data_buffer = NULL;
        }
    }    
}


static void
_resolve_row(lua_State* L, struct xml_node* node, int ret_tbl_idx, struct xml_params* params) {
    enum e_xml_node_type nt = node->nt;
    if(nt == node_element) {
        struct xml_element* element = &node->value.element_value;
        if(_check_name(&element->tag, "Row")) {
            params->row++;
            luaL_checkstack(L, 4, NULL);
            // set empty row
            struct xml_str* row_index = _get_propertyvalue(element->attrs, "ss:Index");
            if(row_index) {
                lua_pushxmlstr(L, row_index);
                int isnum;
                lua_Integer cidx = lua_tointegerx(L, -1, &isnum);
                if(!isnum || cidx < params->row) {
                    luaL_error(L, "invalid row ss:Index");
                }
                lua_pop(L, 1);
                while(cidx > params->row) {
                    lua_newtable(L);
                    unsigned int i=0;
                    for(i=1; i<= params->max_col_of_row1; i++) {
                        lua_pushstring(L, "");
                        lua_seti(L, -2, i);
                    }
                    lua_seti(L, ret_tbl_idx, params->row);
                    params->row++;
                }
            }
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_seti(L, ret_tbl_idx, params->row);
            int row_idx = lua_gettop(L);
            params->cell = 0;
            do_resolve_children(element, _resolve_cell, row_idx, params);
            // first line
            if(params->max_col_of_row1 == 0) {
                // adjust first line
                unsigned int i=0;
                for(i=params->cell; i>=1; i--) {
                    size_t l=0;
                    lua_geti(L, row_idx, i);
                    lua_tolstring(L, -1, &l);
                    lua_pop(L, 1);
                    if(l>0) {
                        break;
                    } else {
                        lua_pushnil(L);
                        lua_seti(L, row_idx, i);
                    }
                }
                params->max_col_of_row1 = i;

            } else {
                unsigned int max_col = params->max_col_of_row1;
                unsigned int i;
                for(i=params->cell+1; i<= max_col; i++) {
                    lua_pushstring(L, "");
                    lua_seti(L, row_idx, i);
                }
            }
            params->cell = 0;
            lua_pop(L, 1);
        }
    }
}


static void
_resolve_table(lua_State* L, struct xml_node* node, int ret_tbl_idx, struct xml_params* params) {
    enum e_xml_node_type nt = node->nt;
    if(nt == node_element) {
        struct xml_element* element = &node->value.element_value;
        if(_check_name(&element->tag, "Table")) {
            params->table++;
            luaL_checkstack(L, 4, NULL);
            struct xml_str* col_count = _get_propertyvalue(element->attrs, "ss:ExpandedColumnCount");
            struct xml_str* row_count = _get_propertyvalue(element->attrs, "ss:ExpandedRowCount");
            if(!col_count || !row_count) {
                luaL_error(L, "invalid col and row of table");
            }
            int col_isnum;
            int row_isnum;
            lua_pushxmlstr(L, col_count);
            lua_pushxmlstr(L, row_count);
            lua_Integer col = lua_tointegerx(L, -2, &col_isnum);
            lua_Integer row = lua_tointegerx(L, -1, &row_isnum);
            if(!col_isnum || !row_isnum) {
                luaL_error(L, "invalid col and row number of table");
            }
            lua_pop(L, 2);
            lua_newtable(L);
            lua_pushinteger(L, col);
            lua_setfield(L, -2, "col");
            lua_pushinteger(L, row);
            lua_setfield(L, -2, "row");
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_insert(L, -3);
            lua_setfield(L, -2, "data");

            lua_seti(L, ret_tbl_idx, params->table);
            int table_idx = lua_gettop(L);
            params->row = 0;
            params->max_col_of_row1 = 0;
            do_resolve_children(element, _resolve_row, table_idx, params);
            params->row = 0;
            params->max_col_of_row1 = 0;
            lua_pop(L, 1);
        }
    }
}


static void
_resolve_worksheet(lua_State* L, struct xml_node* node, int ret_tbl_idx, struct xml_params* params) {
    enum e_xml_node_type nt = node->nt;
    if(nt == node_element) {
        struct xml_element* element = &node->value.element_value;
        if(_check_name(&element->tag, "Worksheet")) {
            params->worksheet++;
            struct xml_str* sheetname = _get_propertyvalue(element->attrs, "ss:Name");
            if(!sheetname) {
                luaL_error(L, "invalid sheetname of worksheet");
            }
            luaL_checkstack(L, 4, NULL);
            lua_newtable(L);
            lua_pushlstring(L, sheetname->str, sheetname->size);
            lua_setfield(L, -2, "name");

            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_insert(L, -3);
            lua_setfield(L, -2, "tables");

            lua_seti(L, ret_tbl_idx, params->worksheet);
            int top = lua_gettop(L);
            params->table = 0;
            do_resolve_children(element, _resolve_table, top, params);
            params->table = 0;
            lua_pop(L, 1);
        }
    }
}


inline static struct xml_node*
__zxml_parser(lua_State* L) {
    size_t source_len = 0;
    const char* source = luaL_checklstring(L, 1, &source_len);
    size_t new_memory_size = source_len*8;
    _init_context(L, new_memory_size);

    struct xml_node* root = xml_parser(get_context(), source, source_len);
    if(!root) {
        luaL_error(L, "zxml parser %s", xml_geterror(get_context()));
    } else if(root->nt != node_element) {
        luaL_error(L, "zxml error root node type:%d", root->nt);
    }
    return root;
}


static int
_lua_zxml_parser_excel_xml2003(lua_State* L) {
    struct xml_node* root = __zxml_parser(L);
    lua_newtable(L); // for worksheets
    int top = lua_gettop(L);
    struct xml_params excel_params = {0};
    excel_params.worksheet = 0;
    struct xml_element* element = &root->value.element_value;
    do_resolve_children(element, _resolve_worksheet, top, &excel_params);
    excel_params.worksheet = 0;
    return 1;
}


static void
_resolve_node(lua_State* L, struct xml_node* node, int ret_tbl_idx, struct xml_params* params) {
    enum e_xml_node_type nt = node->nt;
    params->children++;
    if(nt == node_element) {
        struct xml_element* element = &node->value.element_value;
        luaL_checkstack(L, 4, NULL);
        lua_newtable(L);
        lua_pushxmlstr(L, &element->tag);
        lua_setfield(L, -2, "tag");
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, "attrs");
        struct xml_property* p = element->attrs;
        while(p) {
            lua_pushxmlstr(L, &p->field_name);
            lua_pushxmlstr(L, &p->field_value);
            lua_settable(L, -3);
            p = p->next;
        }
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, "children");
        int top = lua_gettop(L);
        unsigned int bak = params->children;
        params->children = 0;
        do_resolve_children(element, _resolve_node, top, params);
        params->children = bak;
        lua_pop(L, 1);

    } else if (nt == node_content) {
        struct xml_str* content = &node->value.content_value;
        luaL_Buffer B;
        luaL_buffinit(L, &B);
        _do_content(L, content, &B);
        luaL_pushresult(&B);

    } else {
        luaL_error(L, "invalid xml node type:%d", nt);
    }

    if(ret_tbl_idx > 0) {
        lua_seti(L, ret_tbl_idx, params->children);
    }
}


static int
_lua_zxml_parser(lua_State* L) {
    struct xml_node* root = __zxml_parser(L);
    struct xml_params node_params = {0};
    node_params.children = 0;
    _resolve_node(L, root, -1, &node_params);
    node_params.children = 0;
    return 1;
}


int
luaopen_zxml_core(lua_State* L) {
    luaL_checkversion(L);
    luaL_Reg l[] = {
        {"zxml_parser_excel_xml2003", _lua_zxml_parser_excel_xml2003},
        {"zxml_parser", _lua_zxml_parser},
        {NULL, NULL},
    };
    luaL_newlib(L, l);
    return 1;
}

