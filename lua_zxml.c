#include "zxml.h"
#include <stdlib.h>
#include <string.h>
#include <lua.h>
#include <lauxlib.h>
#include <stdbool.h>

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


static void
_resolve_content(lua_State* L, struct xml_node* node, int ret_tbl_idx, struct xml_params* params) {
    enum e_xml_node_type nt = node->nt;
    if(nt == node_element) {
        struct xml_element* element = &node->value.element_value;
        do_resolve_children(element, _resolve_content, ret_tbl_idx, params);
    } else if (nt == node_content) {
        struct xml_str* content = &node->value.content_value;
        luaL_addlstring(params->data_buffer, content->str, content->size);
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
                    luaL_error(L, "invalid ss:Index");
                }
                lua_pop(L, 1);
                while(cidx > params->cell) {
                    lua_pushstring(L, "");
                    lua_seti(L, ret_tbl_idx, params->cell);
                    params->cell++;
                }
            }
            luaL_Buffer B;
            params->data_buffer = &B;
            luaL_buffinit(L, &B);
            do_resolve_children(element, _resolve_data, ret_tbl_idx, params);
            luaL_pushresult(&B);
            // printf("decode data:%s\n", lua_tostring(L, -1));
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
            luaL_checkstack(L, 2, NULL);
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_seti(L, ret_tbl_idx, params->row);
            int row_idx = lua_gettop(L);
            params->cell = 0;
            do_resolve_children(element, _resolve_cell, row_idx, params);
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
            do_resolve_children(element, _resolve_row, table_idx, params);
            params->row = 0;
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


static int
_lua_zxml_parser_excel_xml2003(lua_State* L) {
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

    lua_newtable(L); // for worksheets
    int top = lua_gettop(L);
    struct xml_params excel_params = {0};
    excel_params.worksheet = 0;
    struct xml_element* element = &root->value.element_value;
    do_resolve_children(element, _resolve_worksheet, top, &excel_params);
    excel_params.worksheet = 0;
    return 1;
}



int
luaopen_zxml_core(lua_State* L) {
    luaL_checkversion(L);
    luaL_Reg l[] = {
        {"zxml_parser_excel_xml2003", _lua_zxml_parser_excel_xml2003},
        {NULL, NULL},
    };
    luaL_newlib(L, l);
    return 1;
}

